#!/usr/bin/env python3
"""Original tall-font glyph runs against em_message_glyph_original.c.

docs/MESSAGE_GLYPH.md. The EE interpreter of test_player_slide_reference.py
executes the ORIGINAL instructions over captured EE RAM (user-local, never
copied here): 001FC7B0, 001CC1E0, 001CBE10, 001CC8A0, 001CCE80, 001CC3B0,
001CCB00, 001232E0 and the memset 00121A28. Nothing is hooked away: the two
boundary routines are observed (their arguments recorded) and then executed.

  001CC8A0  glyph texel upload. Each call's arguments (mode, strip x, strip
            y, glyph byte offset) are compared in order with the native
            upload worker's.
  001CC3B0  strip flush. Each call's eight arguments are compared with the
            native flush, and the packet bytes it appends to the render
            context (slot 1 cursor at *D_00275670 + 0x14) are compared with
            an image built from the native register values: both DMA refs,
            the cnt tag, the VIF DIRECT code and every pass's GIF tag, RGBAQ,
            UV and XYZ2 dwords. Bytes the original leaves unwritten keep the
            fill pattern on both sides.

Also compared: the cursor advance per call, and the set of addresses the
original stores to (only the packet area, the cursor word and the stack).

Real data: every string of the global (D_0028A4E8) and area (D_0028A594)
message banks of the roger-encounter capture, drawn through D_00264CD0 with
the captured style D_00275C50 and with record styles, and through the
D_00264BF0 template (no style). Synthetic strings of this file's own making
reach every byte value, the 0x81 expansion, newline recursion, the 512-texel
strip wrap and wrapping coordinates. 001CBE10 runs for every byte value, and
001CC3B0 runs directly over an argument grid.

No original instruction bytes, disassembly, text or font data are written by
this file; the report in build/ holds only counts.
"""
import ctypes as C
import hashlib
import json
import random
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_player_slide_reference import s32  # noqa: E402
from test_message_draw_reference import DrawEE, Style, Config, bank_extent, bank_ptr  # noqa: E402
from reference_mode import MODE, banner, select  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
REFERENCE = DECOMP / 'build/startup-reference'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'

FC7B0, CC1E0, ADVANCE, UPLOAD, FLUSH = 0x1FC7B0, 0x1CC1E0, 0x1CBE10, 0x1CC8A0, 0x1CC3B0
EXECUTED = ((0x1FC7B0, 0x200), (0x1CC1E0, 0x1CC), (0x1CBE10, 0x35C), (0x1CC8A0, 0x254),
            (0x1CC3B0, 0x4E8), (0x1CCB00, 0x8), (0x1CCE80, 0xEC), (0x1232E0, 0x138),
            (0x121A28, 0xC0))
CONTEXT = 0x275670                       # D_00275670: the render context pointer
BANK_G, BANK_A = 0x28A4E8, 0x28A594
LINE_CFG, TEMPLATE, STYLE = 0x264CD0, 0x264BF0, 0x275C50
PACKET_SPRITE, PACKET_SKEWED, PACKET_HEAD = 0x2510C0, 0x251140, 0x2511C0
TEXT, PACKET = 0x1F30000, 0x1F40000
PACKET_SIZE = 0x40000
FILL = 0xA5


# ---------------------------------------------------------------- original
class Original:
    def __init__(self, elf, ram, spad=None):
        self.ee = ee = DrawEE(elf, ram, spad)
        self.context = ee.load(CONTEXT)
        assert self.context, 'D_00275670 is 0 in the capture'
        self.cursor_cell = self.context + 0x14            # slot 1
        self.events, self.stores = [], set()
        ee.hooks[UPLOAD] = lambda e: self.observe(e, UPLOAD, 'upload', 4)
        ee.hooks[FLUSH] = lambda e: self.observe(e, FLUSH, 'flush', 8)
        save = ee.save

        def guarded(address, value, size=4):
            address &= 0xFFFFFFFF
            if not 0x7F000000 <= address < 0x7F100000:
                self.stores.update(range(address, address + size))
            save(address, value, size)
        ee.save = guarded

    def reset_packet(self):
        ee = self.ee
        ee.write(PACKET, bytes([FILL]) * PACKET_SIZE)
        ee.save(self.cursor_cell, PACKET)
        self.stores.clear()

    def observe(self, e, entry, kind, count):
        args = tuple(s32(e.r[4 + i]) for i in range(count))
        before = e.load(self.cursor_cell)
        hook = e.hooks.pop(entry)
        try:
            e.nested(entry)
        finally:
            e.hooks[entry] = hook
        after = e.load(self.cursor_cell)
        assert PACKET <= before <= after <= PACKET + PACKET_SIZE, (kind, hex(before), hex(after))
        self.events.append((kind, args, e.read(before, after - before)))

    def call(self, entry, *args):
        self.ee.call(entry, [a & 0xFFFFFFFF for a in args])
        return s32(self.ee.r[2])

    def unmodelled(self):
        allowed = set(range(PACKET, PACKET + PACKET_SIZE)) | set(range(self.cursor_cell, self.cursor_cell + 4))
        return sorted(a for a in self.stores if a not in allowed)


# ---------------------------------------------------------------- native
class Upload(C.Structure):
    _fields_ = [('x', C.c_int32), ('y', C.c_int32), ('offset', C.c_uint32)]


class Pass(C.Structure):
    _fields_ = [('rgbaq', C.c_uint64), ('uv', C.c_uint64 * 4), ('xyz2', C.c_uint64 * 4)]


class Flush(C.Structure):
    _fields_ = [('slot', C.c_int32), ('x', C.c_int32), ('y', C.c_int32), ('u_end', C.c_int32),
                ('v_end', C.c_int32), ('width', C.c_int32), ('height', C.c_int32),
                ('style', C.POINTER(Style)), ('kind', C.c_int), ('vertex_count', C.c_int32),
                ('pass_', Pass * 5), ('uploads', C.POINTER(Upload)), ('upload_count', C.c_uint32)]


UPLOAD_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Upload))
FLUSH_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Flush))


class Workers(C.Structure):
    _fields_ = [('context', C.c_void_p), ('upload', UPLOAD_FN), ('flush', FLUSH_FN)]


class Glyph(C.Structure):
    _fields_ = [('workers', Workers), ('strip', Upload * 0x100), ('strip_count', C.c_uint32),
                ('fault', C.c_char_p)]


def library():
    out = ROOT / 'build/message_glyph_reference'
    out.mkdir(parents=True, exist_ok=True)
    path = out / ('message_glyph.dylib' if sys.platform == 'darwin' else 'message_glyph.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
                    '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc',
                    'src/game/em_message_glyph_original.c', '-o', str(path)], cwd=ROOT, check=True)
    lib = C.CDLL(str(path))
    lib.em_message_glyph_init.argtypes = [C.POINTER(Glyph), C.POINTER(Workers)]
    lib.em_message_glyph_advance.argtypes = [C.c_int32]
    lib.em_message_glyph_advance.restype = C.c_int32
    lib.em_message_glyph_fc7b0.argtypes = [C.POINTER(Glyph), C.c_int32, C.c_int32, C.c_void_p, C.c_uint32,
                                           C.POINTER(Config)]
    lib.em_message_glyph_cc1e0.argtypes = [C.POINTER(Glyph), C.c_int32, C.c_int32, C.c_int32, C.c_int32,
                                           C.c_int32, C.c_void_p, C.c_uint32, C.POINTER(Style)]
    lib.em_message_glyph_cc3b0.argtypes = [C.c_int32] * 7 + [C.POINTER(Style), C.POINTER(Flush)]
    return lib


class Native:
    def __init__(self, lib, ram):
        self.lib, self.events = lib, []
        self.style = Style.from_buffer_copy(bytes(ram[STYLE:STYLE + 8]))
        words = struct.unpack_from('<5i', ram, LINE_CFG)
        assert struct.unpack_from('<I', ram, LINE_CFG + 0x14)[0] == STYLE, 'D_00264CE4 is not &D_00275C50'
        self.config = Config((C.c_int32 * 5)(*words), C.pointer(self.style))
        template = struct.unpack_from('<5i', ram, TEMPLATE)
        assert struct.unpack_from('<I', ram, TEMPLATE + 0x14)[0] == 0, 'template style pointer'
        self.template = Config((C.c_int32 * 5)(*template), None)
        self.workers = Workers(None, UPLOAD_FN(self.upload), FLUSH_FN(self.flush))
        self.glyph = Glyph()
        assert lib.em_message_glyph_init(C.byref(self.glyph), C.byref(self.workers)) == 1

    def upload(self, _, u):
        u = u.contents
        self.events.append(('upload', (1, u.x, u.y, u.offset), None))
        return 1

    def flush(self, _, f):
        f = f.contents
        style = C.addressof(f.style.contents) if f.style else 0
        assert style in (0, C.addressof(self.style)), 'flush style is not the bound style block'
        args = (f.slot, f.x, f.y, f.u_end, f.v_end, f.width, f.height, STYLE if style else 0)
        passes = [(p.rgbaq, tuple(p.uv), tuple(p.xyz2)) for p in f.pass_]
        uploads = [(f.uploads[i].x, f.uploads[i].y, f.uploads[i].offset) for i in range(f.upload_count)]
        self.events.append(('flush', args, (f.kind, f.vertex_count, passes, uploads)))
        return 1

    @property
    def fault(self):
        return self.glyph.fault


def flush_image(kind, vertices, passes):
    """The packet 001CC3B0 appends, built from the native register values.
    Bytes the original does not write keep FILL."""
    image = bytearray([FILL]) * (0x40 + len(passes) * (0x60 if kind else 0x40))

    def tag(at, qwc, ident, address):
        struct.pack_into('<H', image, at, qwc)
        image[at + 3] = ident
        struct.pack_into('<I', image, at + 4, address)
    tag(0x00, 3, 0x30, PACKET_HEAD)
    tag(0x10, 8, 0x30, PACKET_SKEWED if kind else PACKET_SPRITE)
    tag(0x20, 0x1F if kind else 0x15, 0x10, 0)
    image[0x30:0x40] = bytes(16)
    struct.pack_into('<I', image, 0x3C, 0x5000001E if kind else 0x50000014)
    giftag = 0x8001 | ((0x94000000 if kind else 0x54000000) << 32)
    regs = (0x34343431 | 4 << 32) if kind else 0x43431
    at = 0x40
    for rgbaq, uv, xyz2 in passes:
        words = [giftag, regs, rgbaq]
        for v in range(vertices):
            words += [uv[v], xyz2[v]]
        struct.pack_into('<%dQ' % len(words), image, at, *words)
        at += 0x60 if kind else 0x40
    return bytes(image)


def same(o, n, where):
    want, got = o.events, n.events
    assert len(got) == len(want), (where, 'call count', len(got), len(want))
    strip, flushes = [], 0
    for i, ((wk, wa, packet), (gk, ga, detail)) in enumerate(zip(want, got)):
        assert wk == gk and wa == ga, (where, i, wk, wa, gk, ga)
        if wk == 'upload':
            assert len(packet) == 0x280, (where, i, 'upload packet size', len(packet))
            strip.append(wa[1:])
            continue
        kind, vertices, passes, uploads = detail
        assert vertices == (4 if kind else 2), (where, i)
        assert uploads == strip, (where, i, 'strip contents')
        strip = []
        image = flush_image(kind, vertices, passes)
        assert packet == image, (where, i, 'flush packet',
                                 next(j for j in range(min(len(packet), len(image))) if packet[j] != image[j])
                                 if len(packet) == len(image) else (len(packet), len(image)))
        flushes += 1
    assert n.fault is None, (where, n.fault)
    uploads = sum(1 for e in want if e[0] == 'upload')
    o.events.clear()
    n.events.clear()
    return uploads, flushes


def set_style(o, n, raw):
    o.ee.write(STYLE, raw)
    C.memmove(C.byref(n.style), raw, 8)


def run_text(o, n, text, x, y, cfg, where):
    o.reset_packet()
    o.ee.write(TEXT, text + b'\0')
    o.call(FC7B0, x, y, TEXT, cfg)
    buf = C.create_string_buffer(text, len(text) + 1)
    rc = n.lib.em_message_glyph_fc7b0(C.byref(n.glyph), x, y, buf, len(text) + 1,
                                      C.byref(n.config if cfg == LINE_CFG else n.template))
    assert rc == 0, (where, n.fault)
    assert not o.unmodelled(), (where, [hex(a) for a in o.unmodelled()[:8]])
    return same(o, n, where)


def bank_strings(ram, cell):
    table = bank_ptr(ram, cell)
    size = bank_extent(ram, table)
    w = lambda a: struct.unpack_from('<I', ram, a)[0]
    h = w(table) + w(table + 8)
    out = []
    for i in range(w(table + h + 4)):
        start = table + h + w(table + h) + w(table + h + 0x10 + 16 * i)
        end = ram.index(b'\0', start)
        assert end < table + size
        out.append(bytes(ram[start:end]))
    return out


def synthetic_strings():
    every = bytes(range(1, 256))
    return [
        b'', b'A', b'HELLO WORLD', b'$100 AND $5', every[:0x7E], every[0x7E:0xBE], b'\x01' + every[0xBE:],
        b'ONE\nTWO\nTHREE', b'\nLEAD', b'TAIL\n', b'\n\n\n',
        b'CTRL\x01\x02\x1fRUN\x0cX', b'SKIP\x80\xa0\xdf\xf0\xffEND', b'E\xe0\xef\x9f\x81\x41\x81\x81Z',
        b'TRAILING\x81', b'\x81', b'\x81\x81\x81',
        b'M' * 0x7E, b'W' * 0x2A + b'm' * 0x2A + b'w' * 0x2A, b'.' * 0x7E + b'\nNEXT',
        b'i' * 0x7E, b'MIXED \x81A\nLINE \x01TWO\x7f\x80\x81\xa0',
        b'M' * 42 + b'(' + b'AB', b'M' * 42 + b'(', b'\x01\xf5X\x02\xf0\xffY\x03\xef\x04\x80Q',
    ]


def advance_cases(o, lib):
    values = list(range(256)) + [-1, 0x100, 0x7FFFFFFF, -0x80000000, 0x124]
    for c in values:
        assert lib.em_message_glyph_advance(c) == o.call(ADVANCE, c), hex(c)
    return len(values)


def flush_grid(o, n, lib):
    """001CC3B0 called directly over an argument grid."""
    rnd = random.Random(0x1CC3B0)
    styles = [None, bytes([0x60, 0x60, 0x60, 0, 0x80, 0, 0, 0]), bytes([0x60, 0x60, 0x60, 0, 0x80, 8, 0, 0]),
              bytes([0x10, 0x20, 0x30, 0x40, 0x7F, 1, 0, 5]), bytes([0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0x10]),
              bytes([0x01, 0x02, 0x03, 0x84, 0x00, 0, 0, 0x0F]), bytes([0, 0, 0, 0, 0x81, 0x80, 0xAA, 0xFF]),
              bytes([0x40, 0x41, 0x42, 0x43, 0x80, 0, 0x33, 0x10])]
    coords = [(0x700, 0x790), (0x7C0, 0x852), (0, 0), (-1, -1), (0x7FFFFFFF, 0x7FFFFFFF), (-0x80000000, 5)]
    extents = [(1, 0x14, 0, 0x14), (0x201, 0x14, 0x200, 0x14), (0x55, 0x14, 0x54, 0x28), (0, 0, 0, -3),
               (0x7FFFFFFF, -1, -0x80000000, 0x7FFFFFFF)]
    cases = [(s, c, e) for s in range(len(styles)) for c in coords for e in extents]
    fixed = len(cases)
    cases += [(rnd.randrange(len(styles)), (rnd.getrandbits(32) - (1 << 31), rnd.getrandbits(32) - (1 << 31)),
               tuple(rnd.getrandbits(32) - (1 << 31) for _ in range(4))) for _ in range(400)]
    cases = select(cases, 60, 0x1CC3B0, axes=(lambda c: c[0],),
                   keep=lambda i, c: i < fixed and (c[1] in coords[:2] or c[2] == extents[0]))
    for index, (s, (x, y), (u, v, w, h)) in enumerate(cases):
        raw = styles[s]
        if raw is not None:
            set_style(o, n, raw)
        o.reset_packet()
        o.call(FLUSH, 1, x, y, u, v, w, h, STYLE if raw is not None else 0)
        packet = o.ee.read(PACKET, o.ee.load(o.cursor_cell) - PACKET)
        f = Flush()
        lib.em_message_glyph_cc3b0(1, x, y, u, v, w, h, C.pointer(n.style) if raw is not None else None,
                                   C.byref(f))
        passes = [(p.rgbaq, tuple(p.uv), tuple(p.xyz2)) for p in f.pass_]
        assert f.vertex_count == (4 if f.kind else 2), index
        assert packet == flush_image(f.kind, f.vertex_count, passes), ('flush grid', index, s, x, y, u, v, w, h)
        assert not o.unmodelled(), ('flush grid', index)
    o.events.clear()
    return len(cases)


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'wrong original executable'
    capture = REFERENCE / 'roger-encounter'
    ram = (capture / 'eeMemory.bin').read_bytes()
    text_base = 0x300 - 0x100000
    for address, size in EXECUTED:
        assert ram[address:address + size] == elf[address + text_base:address + text_base + size], hex(address)
    for address, size in ((LINE_CFG, 0x18), (TEMPLATE, 0x18), (PACKET_SPRITE, 0x80), (PACKET_SKEWED, 0x80),
                          (PACKET_HEAD, 0x30)):
        assert ram[address:address + size] == elf[address + text_base:address + text_base + size], hex(address)
    lib = library()
    o, n = Original(elf, ram), Native(lib, ram)
    captured_style = bytes(ram[STYLE:STYLE + 8])

    report = {'mode': MODE}
    report['advance_values'] = advance_cases(o, lib)
    report['flush_grid_cases'] = flush_grid(o, n, lib)

    real = [(b'G', i, s) for i, s in enumerate(bank_strings(ram, BANK_G))]
    real += [(b'A', i, s) for i, s in enumerate(bank_strings(ram, BANK_A))]
    styles = [captured_style, captured_style[:5] + bytes([8]) + captured_style[6:],
              bytes([0x60, 0x40, 0x20, 0x00, 0x70, 0, 0, 3])]
    positions = [(0x100 - 0x50, 0xC2), (0x10, 0x20), (-0x30, -0x10)]
    cases = [(bank, index, text, style, config, pos)
             for bank, index, text in real
             for style, config in ((0, LINE_CFG), (1, LINE_CFG), (2, LINE_CFG), (0, TEMPLATE))
             for pos in range(len(positions))]
    cases = select(cases, 220, 0x1FC7B0, axes=(lambda c: c[3], lambda c: c[4], lambda c: c[5]),
                   keep=lambda i, c: c[5] == 0 and c[3] == 0 and c[4] == LINE_CFG)
    banner(f'{len(cases)} real bank strings x styles x positions (every string at the message position '
           f'with the captured style; the rest sampled in quick mode)')
    uploads = flushes = 0
    for bank, index, text, style, config, pos in cases:
        set_style(o, n, styles[style])
        x, y = positions[pos]
        u, f = run_text(o, n, text, x, y, config, (bank, index, style, hex(config), pos))
        uploads, flushes = uploads + u, flushes + f
    report['real_string_cases'] = len(cases)
    report['real_uploads'], report['real_flushes'] = uploads, flushes

    uploads = flushes = runs = 0
    synthetic = [(t, style, config, pos) for t in synthetic_strings() for style in range(len(styles))
                 for config in (LINE_CFG, TEMPLATE) for pos in ((0x40, 0xC2), (0x7FFFFFF0, 0x7FFFFF00), (-0x800, 3))]
    synthetic = select(synthetic, 70, 0x1CC1E0, axes=(lambda c: c[1], lambda c: c[2], lambda c: c[3]),
                       keep=lambda i, c: c[1] == 0 and c[2] == LINE_CFG and c[3] == (0x40, 0xC2))
    for text, style, config, (x, y) in synthetic:
        set_style(o, n, styles[style])
        u, f = run_text(o, n, text, x, y, config, ('synthetic', text[:12], style, hex(config), x))
        uploads, flushes, runs = uploads + u, flushes + f, runs + 1
    report['synthetic_runs'], report['synthetic_uploads'], report['synthetic_flushes'] = runs, uploads, flushes
    assert flushes > runs, 'the strip wrap was not reached'
    report['boundaries'] = ('001CC8A0 texels (the port glyph atlas) and the GS rasterisation of the packed '
                            'passes are not compared here')
    out = ROOT / 'build/message_glyph_reference'
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
