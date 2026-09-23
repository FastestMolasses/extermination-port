#!/usr/bin/env python3
"""Original message line layout/draw against em_message_draw_original.c.

docs/MESSAGE_DRAW.md. The EE interpreter of test_player_slide_reference.py
executes the ORIGINAL instructions over captured EE RAM (user-local, never
copied here): the 001FD950 draw prefix (driven through a scratch message
entity whose timer path skips the flag mailbox), 001FE070, 001FE530,
001FE460/480/4B0/4D0, 001CC170, 001FC770, 001232E0 and the memset 00121A28.
Two boundaries are hooked on the original side and are workers on the
native side, and every call to them is compared in order:

  001CBE10  glyph advance. Its answers come from executing the original
            001CBE10 for every byte value (table built at start-up), so both
            sides see original advances.
  001FC7B0  glyph-run draw. Each call is compared on x, y, the text bytes up
            to the NUL, which config (D_00264CD0 or the D_00264BF0 template
            copy), the five config words and the eight style bytes behind
            its +0x14 pointer at the moment of the call.

Also compared: every return value, the D_00275C50..57 style bytes, the
D_00820ED0 / D_00820F90 buffers, and the set of addresses the original
stores to (each must be a byte the native module models).

Real data: the global (D_0028A4E8) and area (D_0028A594) banks of the
roger-encounter capture, checked byte-identical in the other captures that
hold them. Synthetic banks (written by this file, plain ASCII of its own)
reach the record tags the area-11 banks never use (0, 2, 4, other), repeated
triggers, triggers past the text, and newlines inside record lines.

No original instruction bytes, disassembly, text or bank data are written
by this file; the report in build/ holds only counts.
"""
import ctypes as C
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_player_slide_reference import EE, s32, sx32  # noqa: E402
from reference_mode import MODE, banner, select  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
REFERENCE = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'

FD950, FE070, FE530, CC170, FC770, STRLEN = 0x1FD950, 0x1FE070, 0x1FE530, 0x1CC170, 0x1FC770, 0x1232E0
FE460, FE480, FE4B0, FE4D0 = 0x1FE460, 0x1FE480, 0x1FE4B0, 0x1FE4D0
ADVANCE, DRAW, FACE = 0x1CBE10, 0x1FC7B0, 0x1D06E0
# Executed routines (address, size): their bytes must equal the ELF's.
EXECUTED = ((0x1FD950, 0x22C), (0x1FE070, 0x3F0), (0x1FE460, 0x18), (0x1FE480, 0x30),
            (0x1FE4B0, 0x18), (0x1FE4D0, 0x5C), (0x1FE530, 0x124), (0x1CC170, 0x64),
            (0x1CBE10, 0x35C), (0x1FC770, 0x40), (0x1232E0, 0x138), (0x121A28, 0xC0))
BANK_G, BANK_A = 0x28A4E8, 0x28A594
LINE_CFG, TEMPLATE, STYLE, COLORS = 0x264CD0, 0x264BF0, 0x275C50, 0x26EC10
MEASURE, LINE = 0x820ED0, 0x820F90
ENTITY, SYNTH, SCRATCH = 0x1F10000, 0x1F00000, 0x1F20000
MODELLED = (set(range(MEASURE, MEASURE + 0xC0)) | set(range(LINE, LINE + 0x80)) |
            set(range(STYLE, STYLE + 8)))
# The in-scope message lines (docs/FIRST_LEVEL_ROUTE.md beats): panel
# 0x80000018 (00/03), refusal 0x8000001A (02), director 0x97/0x99 (11/13),
# Roger chain from 0x7F (10). The encounter's captured line is added from
# the capture itself.
SCOPE = (0x80000018, 0x8000001A, 0x97, 0x99, 0x7F, 0x80, 0x81, 0x82)
OTHER_CAPTURES = [REFERENCE / p for p in ('opening_ee.bin', 'playable_ee.bin', 'elevator/refusal/eeMemory.bin',
                                          'panel/eeMemory.bin', 'status-hub/eeMemory.bin')]
OTHER_CAPTURES += [ROUTE / b / 'eeMemory.bin' for b in ('00_panel_no_battery', '02_elevator_refusal',
                                                        '10_cage_roof_roger', '14_roger_encounter')]


# ---------------------------------------------------------------- oracle
class DrawEE(EE):
    """EE plus the two MMI forms 001232E0 / 00121A28 use beyond the base set."""

    def mmi(self, word, pc):
        fn, sub = word & 63, word >> 6 & 31
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        full = lambda n: (self.r[n] & 0xFFFFFFFFFFFFFFFF) | (self.rh[n] << 64)
        if fn == 0x08 and sub == 0x09:                        # psubb
            a, b = full(rs), full(rt)
            value = sum((((a >> 8 * i) - (b >> 8 * i)) & 255) << 8 * i for i in range(16))
        elif fn == 0x29 and sub == 0x1B:                      # pcpyh
            b = full(rt)
            lo, hi = b & 0xFFFF, b >> 64 & 0xFFFF
            value = sum(lo << 16 * i for i in range(4)) | sum(hi << 16 * i for i in range(4, 8))
        else:
            return super().mmi(word, pc)
        if rd:
            self.r[rd] = value & 0xFFFFFFFFFFFFFFFF
            self.rh[rd] = value >> 64

    def text(self, address, limit=0x400):
        data = self.read(address, limit)
        end = data.find(b'\0')
        assert end >= 0, ('unterminated original text', hex(address))
        return data[:end]


class Original:
    def __init__(self, elf, ram, spad, advances):
        self.ee = ee = DrawEE(elf, ram, spad)
        self.log, self.stores = [], set()
        ee.hooks[ADVANCE] = self.advance
        ee.hooks[DRAW] = self.draw
        ee.hooks[FACE] = lambda e: e.ret_int(0)
        self.advances = advances
        save = ee.save

        def guarded(address, value, size=4):
            address &= 0xFFFFFFFF
            if not 0x7F000000 <= address < 0x7F100000:
                self.stores.update(range(address, address + size))
            save(address, value, size)
        ee.save = guarded

    def advance(self, e):
        c = e.arg(0)
        assert c <= 0xFF and c >= 0x20, hex(c)
        self.log.append(('advance', c))
        e.ret_int(self.advances[c])

    def draw(self, e):
        cfg = e.arg(3)
        words = tuple(s32(e.load(cfg + 4 * i)) for i in range(5))
        style = e.load(cfg + 0x14)
        self.log.append(('draw', s32(e.arg(0)), s32(e.arg(1)), e.text(e.arg(2)),
                         'line' if cfg == LINE_CFG else 'template', words,
                         e.read(style, 8) if style else None))
        e.ret_int(0)

    def call(self, entry, *args):
        ee = self.ee
        ee.call(entry, [a & 0xFFFFFFFF for a in args])
        return s32(ee.r[2])

    def unmodelled(self, extra=()):
        allowed = MODELLED | set(extra)
        return sorted(a for a in self.stores if a not in allowed)


def original_advances(elf, ram):
    """001CBE10 executed for every byte value."""
    ee = DrawEE(elf, ram)
    table = []
    for c in range(256):
        ee.call(ADVANCE, [c])
        table.append(s32(ee.r[2]))
    return table


# ---------------------------------------------------------------- native
class Style(C.Structure):
    _fields_ = [('color', C.c_int32), ('glyph', C.c_uint8), ('flag', C.c_uint8), ('pad', C.c_uint8 * 2)]


class Config(C.Structure):
    _fields_ = [('word', C.c_int32 * 5), ('style', C.POINTER(Style))]


class Bank(C.Structure):
    _fields_ = [('bytes', C.c_void_p), ('size', C.c_uint32)]


class Data(C.Structure):
    _fields_ = [('global_', Bank), ('area', Bank), ('colors', C.POINTER(C.c_int32)),
                ('color_count', C.c_uint32), ('line_config', C.POINTER(Config)),
                ('fallback', C.POINTER(Config)), ('text', C.POINTER(Style))]


ADV_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint8, C.POINTER(C.c_int32))
DRAW_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, C.c_int32, C.c_void_p, C.POINTER(Config))


class Workers(C.Structure):
    _fields_ = [('context', C.c_void_p), ('glyph_advance', ADV_FN), ('draw_text', DRAW_FN)]


class Draw(C.Structure):
    _fields_ = [('data', C.POINTER(Data)), ('workers', Workers), ('measure', C.c_uint8 * 0xC0),
                ('line', C.c_uint8 * 0x80), ('fault', C.c_char_p)]


def library():
    out = ROOT / 'build/message_draw_reference'
    out.mkdir(parents=True, exist_ok=True)
    path = out / ('message_draw.dylib' if sys.platform == 'darwin' else 'message_draw.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
                    '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc',
                    'src/game/em_message_draw_original.c', '-o', str(path)], cwd=ROOT, check=True)
    lib = C.CDLL(str(path))
    lib.em_message_draw_init.argtypes = [C.POINTER(Draw), C.POINTER(Data), C.POINTER(Workers)]
    lib.em_message_draw_line.argtypes = [C.c_void_p, C.c_int, C.c_uint32]
    lib.em_message_draw_fe070.argtypes = [C.POINTER(Draw), C.POINTER(Bank), C.c_int32, C.c_int32, C.c_int32]
    lib.em_message_draw_fe530.argtypes = [C.POINTER(Draw), C.c_void_p, C.c_uint32, C.c_void_p, C.c_uint32,
                                          C.c_int32, C.POINTER(C.c_uint32)]
    lib.em_message_draw_cc170.argtypes = [C.POINTER(Draw), C.c_void_p, C.c_uint32, C.POINTER(C.c_int32)]
    lib.em_message_draw_fc770.argtypes = [C.POINTER(Draw), C.c_int32, C.c_int32, C.c_void_p, C.POINTER(Config)]
    lib.em_message_draw_strlen.argtypes = [C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32)]
    lib.em_message_bank_count.argtypes = [C.POINTER(Bank), C.POINTER(C.c_int32)]
    lib.em_message_bank_string.argtypes = [C.POINTER(Bank), C.c_int32, C.POINTER(C.c_uint32)]
    lib.em_message_bank_records.argtypes = [C.POINTER(Bank), C.c_int32, C.POINTER(C.c_uint32)]
    lib.em_message_bank_record.argtypes = [C.POINTER(Bank), C.c_int32, C.c_uint32, C.POINTER(C.c_int),
                                           C.POINTER(C.c_uint32)]
    return lib


def bank_extent(ram, address):
    """The bytes a bank's own offsets reach: header, entries, records and
    string pool (asserted: every string terminates inside)."""
    w = lambda a: struct.unpack_from('<I', ram, a)[0]
    h = w(address) + w(address + 8)
    size = h + w(address + h) + w(address + h + 8)
    lines = w(address + 4)
    for i in range(w(address + h + 4)):
        start = w(address + h) + h + w(address + h + 0x10 + 16 * i)
        assert ram.index(b'\0', address + start) < address + size, ('string outside bank', i)
    for i in range(lines):
        records = w(address + 0x10 + 16 * i + 0xC) >> 4
        if records:
            assert w(address) + w(address + 0x10 + 16 * i) + 16 * records <= size
    return size


class Native:
    """The native module over the same captured data, logging worker calls."""

    def __init__(self, lib, ram, advances, banks):
        self.lib, self.log, self.advances = lib, [], advances
        self.keep = []
        self.banks = {}
        for name, (address, size) in banks.items():
            buf = C.create_string_buffer(bytes(ram[address:address + size]), size)
            self.keep.append(buf)
            self.banks[name] = Bank(C.cast(buf, C.c_void_p), size)
        self.style = Style.from_buffer_copy(bytes(ram[STYLE:STYLE + 8]))
        words = struct.unpack_from('<5i', ram, LINE_CFG)
        assert struct.unpack_from('<I', ram, LINE_CFG + 0x14)[0] == STYLE, 'D_00264CE4 is not &D_00275C50'
        self.config = Config((C.c_int32 * 5)(*words), C.pointer(self.style))
        template = struct.unpack_from('<5i', ram, TEMPLATE)
        assert struct.unpack_from('<I', ram, TEMPLATE + 0x14)[0] == 0, 'template style pointer'
        self.template = Config((C.c_int32 * 5)(*template), None)
        self.colors = (C.c_int32 * 16)(*struct.unpack_from('<16i', ram, COLORS))
        self.data = Data(self.banks.get('global', Bank()), self.banks.get('area', Bank()), self.colors, 16,
                         C.pointer(self.config), C.pointer(self.template), C.pointer(self.style))
        self.workers = Workers(None, ADV_FN(self.advance), DRAW_FN(self.draw))
        self.draw_state = Draw()
        assert lib.em_message_draw_init(C.byref(self.draw_state), C.byref(self.data), C.byref(self.workers)) == 1
        C.memmove(self.draw_state.measure, bytes(ram[MEASURE:MEASURE + 0xC0]), 0xC0)
        C.memmove(self.draw_state.line, bytes(ram[LINE:LINE + 0x80]), 0x80)

    def advance(self, _, c, out):
        self.log.append(('advance', c))
        out[0] = self.advances[c]
        return 1

    def draw(self, _, x, y, text, cfg):
        c = cfg.contents
        kind = 'line' if C.addressof(c) == C.addressof(self.config) else 'template'
        style = C.string_at(C.cast(c.style, C.c_void_p), 8) if c.style else None
        self.log.append(('draw', x, y, C.string_at(text), kind, tuple(c.word), style))
        return 1

    @property
    def fault(self):
        return self.draw_state.fault


def bank_ptr(ram, cell):
    return struct.unpack_from('<I', ram, cell)[0]


def real_banks(ram):
    g, a = bank_ptr(ram, BANK_G), bank_ptr(ram, BANK_A)
    return {'global': (g, bank_extent(ram, g)), 'area': (a, bank_extent(ram, a))}


def state(original, native):
    """(style, measure, line) bytes on both sides."""
    ee = original.ee
    return ((ee.read(STYLE, 8), ee.read(MEASURE, 0xC0), ee.read(LINE, 0x80)),
            (bytes(native.style), bytes(native.draw_state.measure), bytes(native.draw_state.line)))


def same(original, native, where):
    got, want = native.log, original.log
    assert got == want, (where, next(((i, a, b) for i, (a, b) in enumerate(zip(got, want)) if a != b),
                                     (len(got), len(want))))
    o, n = state(original, native)
    assert o == n, (where, 'style/buffer bytes', [i for i in range(3) if o[i] != n[i]])
    assert native.fault is None, (where, native.fault)
    count = len(want)
    original.log.clear()
    native.log.clear()
    return count


# ---------------------------------------------------------------- synthetic banks
def synthetic_bank(lines):
    """lines: [(text bytes, [(tag, arg, trigger, byte12), ...])] -> bank bytes."""
    n = len(lines)
    records = b''.join(struct.pack('<4i', *r) for _, recs in lines for r in recs)
    rec_size = (len(records) + 15) & ~15
    pool = b''
    offsets = []
    for text, _ in lines:
        offsets.append(len(pool))
        pool += text + b'\0'
    header = struct.pack('<4I', 0x10 + 16 * n, n, rec_size, 0x10)
    entries, at = b'', 0
    for _, recs in lines:
        entries += struct.pack('<4I', at, 0, 0, len(recs) << 4)
        at += 16 * len(recs)
    section = struct.pack('<4I', 0x10 + 16 * n, n, len(pool), 1)
    section += b''.join(struct.pack('<4I', off, 0, 0, 0) for off in offsets)
    body = header + entries + records.ljust(rec_size, b'\0') + section + pool
    return body + bytes(-len(body) % 16)


SYNTHETIC = [
    (b'PLAIN LINE', []),
    (b'TOP\nBOTTOM', []),
    (b'AB CD EF', [(3, 1, 0, 0), (3, 0, 8, 0)]),
    (b'AB\nCD EF\nG', [(3, 1, 0, 0), (3, 0, 4, 0)]),
    (b'COLOR TWO', [(2, 3, 2, 0), (2, 0, 6, 0)]),
    (b'COLOR FOUR', [(4, 5, 0, 2), (4, 0, 5, 0x21)]),
    (b'TAG ZERO SEVEN', [(0, 9, 3, 0), (7, 2, 5, 0)]),
    (b'SAME TRIGGER', [(3, 1, 4, 0), (2, 6, 4, 0), (4, 2, 4, 5), (3, 0, 9, 0)]),
    (b'PAST END', [(3, 1, 2, 0), (3, 0, 11, 0)]),
    (b'NEVER', [(3, 1, 40, 0)]),
    (b'\x01lo\x1fw \x81\xe0\xff', [(3, 2, 1, 0)]),
    (b'', [(3, 1, 0, 0)]),
    (b'', []),
    (b'\n\n', [(3, 1, 1, 0)]),
    (b'X' * 60 + b'\n' + b'Y' * 70, [(3, 1, 10, 0), (3, 0, 65, 0)]),
    (b'SEG\x0cMENT', []),
]


# ---------------------------------------------------------------- cases
def run_lines(elf, ram, spad, lib, advances, lines, label):
    """Sequential 001FD950 draw prefixes on one original and one native."""
    o = Original(elf, ram, spad, advances)
    n = Native(lib, ram, advances, real_banks(ram))
    ee, calls = o.ee, 0
    for line in lines:
        ee.write(ENTITY, bytes(0x9C))
        ee.save(ENTITY + 0x34, line)
        ee.save(ENTITY + 0x6C, 1)
        ee.save(ENTITY + 0x51, 0xFF, 1)
        o.call(FD950, ENTITY)
        assert ee.load(ENTITY + 0x6C) == 0 and s32(ee.r[2]) == 0, (label, hex(line))
        assert lib.em_message_draw_line(C.addressof(n.draw_state), line >> 31, line & 0x7FFFFFFF) == 1, \
            (label, hex(line), n.fault)
        calls += same(o, n, (label, hex(line)))
    assert not o.unmodelled(range(ENTITY, ENTITY + 0x9C)), (label, [hex(a) for a in o.unmodelled()[:8]])
    return calls


def captured_resume(elf, path, spad_path, lib, advances):
    """001FD950 on the capture's own D_002821B0 block (mid-message)."""
    ram = path.read_bytes()
    spad = spad_path.read_bytes() if spad_path and spad_path.exists() else None
    o = Original(elf, ram, spad, advances)
    n = Native(lib, ram, advances, real_banks(ram))
    current = struct.unpack_from('<I', ram, 0x2821B0 + 0x34)[0]
    o.call(FD950, 0x2821B0)
    assert lib.em_message_draw_line(C.addressof(n.draw_state), current >> 31, current & 0x7FFFFFFF) == 1
    draws = sum(1 for e in o.log if e[0] == 'draw')
    return current, draws, same(o, n, ('resume', path.parent.name))


def accessor_cases(elf, ram, lib, native):
    o = Original(elf, ram, None, [0] * 256)
    checked = 0
    for name, cell in (('global', BANK_G), ('area', BANK_A)):
        tbl = bank_ptr(ram, cell)
        bank = native.banks[name]
        count = C.c_int32()
        assert lib.em_message_bank_count(C.byref(bank), C.byref(count)) == 1
        assert count.value == o.call(FE460, tbl)
        lines = struct.unpack_from('<I', ram, tbl + 4)[0]
        for index in range(count.value):
            offset, records = C.c_uint32(), C.c_uint32()
            assert lib.em_message_bank_string(C.byref(bank), index, C.byref(offset)) == 1
            assert (tbl + offset.value) & 0xFFFFFFFF == o.call(FE480, tbl, index) & 0xFFFFFFFF, (name, index)
            assert lib.em_message_bank_records(C.byref(bank), index, C.byref(records)) == 1
            assert records.value == o.call(FE4B0, tbl, index) & 0xFFFFFFFF, (name, index)
            checked += 2
        for index in (-1, 0, 0x69, lines - 1, lines, lines + 3):
            for record in (0, 1, 2, 0xFFFFFFFF):
                found, offset = C.c_int(), C.c_uint32()
                if not lib.em_message_bank_record(C.byref(bank), index, record, C.byref(found), C.byref(offset)):
                    # Only an entry outside the bank may refuse (negative index).
                    assert index < 0, (name, index, record)
                    continue
                want = o.call(FE4D0, tbl, index, record) & 0xFFFFFFFF
                got = (tbl + offset.value) & 0xFFFFFFFF if found.value else 0
                assert got == want, (name, index, record, hex(got), hex(want))
                checked += 1
    return checked


def strlen_cases(elf, ram, lib):
    o = Original(elf, ram, None, [0] * 256)
    cases = [(align, length) for align in range(16) for length in (0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, 40)]
    cases = select(cases, 60, 0x1232E0, axes=(lambda c: c[0], lambda c: c[1]))
    for align, length in cases:
        body = bytes(0x41 + (i % 26) for i in range(length)) + b'\0' + b'\x80' * 40
        o.ee.write(SCRATCH, b'\xAA' * 16)
        o.ee.write(SCRATCH + align, body)
        buf = C.create_string_buffer(body, len(body))
        got = C.c_uint32()
        assert lib.em_message_draw_strlen(buf, len(body), C.byref(got)) == 1
        assert got.value == o.call(STRLEN, SCRATCH + align) == length, (align, length)
    return len(cases)


def string_cases(elf, ram, lib, advances):
    """001CC170 and 001FE530 on strings of this file's own making."""
    o = Original(elf, ram, None, advances)
    n = Native(lib, ram, advances, {})
    strings = [b'', b'A', b'HELLO WORLD', b'\x01\x02AB\x1f\x20', b'\x7f\x80\x81\xa0\xe0\xff',
               b'ONE\nTWO\nTHREE', b'A\x0cB\x0cC', b'\n\nX', b'TAIL\n', b'\x0c', b'MIX\nED\x0cSEP\nZ']
    widths = segments = 0
    for s in strings:
        o.ee.write(SCRATCH, s + b'\0')
        buf = C.create_string_buffer(s, len(s) + 1)
        width = C.c_int32()
        assert lib.em_message_draw_cc170(C.byref(n.draw_state), buf, len(s) + 1, C.byref(width)) == 0
        assert width.value == o.call(CC170, SCRATCH), s
        widths += 1 + same(o, n, ('cc170', s))
        for skip in (0, 1, 2, 3, -1):
            for with_dst in (1, 0):
                o.ee.write(SCRATCH + 0x100, b'\x55' * 0x40)
                dst = (C.c_uint8 * 0x40)(*([0x55] * 0x40))
                nxt = C.c_uint32()
                assert lib.em_message_draw_fe530(C.byref(n.draw_state), dst if with_dst else None, 0x40,
                                                 buf, len(s) + 1, skip, C.byref(nxt)) == 0
                want = o.call(FE530, SCRATCH + 0x100 if with_dst else 0, SCRATCH, skip)
                assert (SCRATCH + nxt.value) == want & 0xFFFFFFFF, (s, skip, with_dst)
                assert bytes(dst) == o.ee.read(SCRATCH + 0x100, 0x40), (s, skip, with_dst)
                segments += 1
    return widths, segments


def fc770_cases(elf, ram, lib, advances):
    o = Original(elf, ram, None, advances)
    n = Native(lib, ram, advances, {})
    calls = 0
    for x, y, cfg in ((10, 20, 0), (-5, 0x7FFFFFFF, LINE_CFG), (0x100, 0xC2, 0)):
        o.ee.write(SCRATCH, b'RUN\0')
        o.call(FC770, x, y, SCRATCH, cfg)
        buf = C.create_string_buffer(b'RUN')
        assert lib.em_message_draw_fc770(C.byref(n.draw_state), x, y, buf,
                                         C.pointer(n.config) if cfg else None) == 0
        calls += same(o, n, ('fc770', x, y, cfg))
    return calls


def fe070_cases(elf, ram, lib, advances):
    """Synthetic banks and out-of-range / extreme arguments."""
    body = synthetic_bank(SYNTHETIC)
    ram = bytearray(ram)
    ram[SYNTH:SYNTH + len(body)] = body
    o = Original(elf, bytes(ram), None, advances)
    n = Native(lib, ram, advances, {'synthetic': (SYNTH, len(body))})
    bank = n.banks['synthetic']
    calls = results = 0
    positions = ((0x40, 0xC2), (-300, -7), (0x7FFFFFF0, 0x7FFFFFFA))
    for index in range(-2, len(SYNTHETIC) + 2):
        for x, y in positions:
            want = o.call(FE070, SYNTH, index, x, y)
            got = lib.em_message_draw_fe070(C.byref(n.draw_state), C.byref(bank), index, x, y)
            assert got == want, (index, x, y, got, want, n.fault)
            calls += same(o, n, ('fe070', index, x, y))
            results += 1
    assert not o.unmodelled(), [hex(a) for a in o.unmodelled()[:8]]
    return results, calls


def fault_fixture():
    out = ROOT / 'build/message_draw_reference'
    binary = out / 'message_draw_test'
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-Isrc', 'tests/message_draw_test.c',
                    'src/game/em_message_draw_original.c', '-o', str(binary)], cwd=ROOT, check=True)
    result = subprocess.run([str(binary)], cwd=ROOT, check=True, capture_output=True, text=True)
    assert result.stdout.strip().endswith('PASS'), result.stdout + result.stderr
    return result.stdout.strip().split()[-2]


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'wrong original executable'
    capture = REFERENCE / 'roger-encounter'
    ram, spad = (capture / 'eeMemory.bin').read_bytes(), (capture / 'scratchpad.bin').read_bytes()
    text_base = 0x300 - 0x100000
    for address, size in EXECUTED:
        assert ram[address:address + size] == elf[address + text_base:address + text_base + size], hex(address)
    lib = library()
    advances = original_advances(elf, ram)

    # The banks, config, template and colour table are the same in every capture.
    g, a = bank_ptr(ram, BANK_G), bank_ptr(ram, BANK_A)
    extents = {g: bank_extent(ram, g), a: bank_extent(ram, a)}
    fixed = [(LINE_CFG, 0x18), (TEMPLATE, 0x18), (COLORS, 0x40)] + list(extents.items())
    for path in OTHER_CAPTURES:
        other = path.read_bytes()
        assert (bank_ptr(other, BANK_G), bank_ptr(other, BANK_A)) == (g, a), path
        for address, size in fixed:
            assert other[address:address + size] == ram[address:address + size], (path, hex(address))

    native = Native(lib, ram, advances, real_banks(ram))
    count_g, count_a = (struct.unpack_from('<I', ram, t + struct.unpack_from('<I', ram, t)[0] +
                                           struct.unpack_from('<I', ram, t + 8)[0] + 4)[0] for t in (g, a))
    recorded = [i for i in range(count_a) if struct.unpack_from('<I', ram, a + 0x10 + 16 * i + 0xC)[0] >> 4]
    lines = [0x80000000 | i for i in range(count_g)] + list(range(count_a))
    encounter = struct.unpack_from('<I', ram, 0x2821B0 + 0x34)[0]
    keep = set(SCOPE) | {encounter, 0x80000000, 0x80000000 | (count_g - 1), 0, count_a - 1} | set(recorded)
    chosen = lines  # every real line: the whole sweep takes about a second
    assert keep <= set(lines), 'an in-scope line is missing from the banks'
    banner(f'every case class runs in both modes ({len(lines)} real lines, all synthetic lines); '
           f'quick mode samples only the strlen alignment x length grid')

    report = {'mode': MODE}
    report['accessor_checks'] = accessor_cases(elf, ram, lib, native)
    report['strlen_cases'] = strlen_cases(elf, ram, lib)
    report['cc170_checks'], report['fe530_cases'] = string_cases(elf, ram, lib, advances)
    report['fc770_calls'] = fc770_cases(elf, ram, lib, advances)
    report['fe070_synthetic_results'], report['fe070_synthetic_calls'] = fe070_cases(elf, ram, lib, advances)
    report['fd950_lines'] = len(chosen)
    report['fd950_worker_calls'] = run_lines(elf, ram, spad, lib, advances, chosen, 'real lines')
    resumes = []
    for path, sp in ((capture / 'eeMemory.bin', capture / 'scratchpad.bin'),
                     (REFERENCE / 'opening_ee.bin', REFERENCE / 'opening_scratchpad.bin'),
                     (REFERENCE / 'elevator/refusal/eeMemory.bin', None)):
        current, draws, calls = captured_resume(elf, path, sp, lib, advances)
        resumes.append({'capture': path.parent.name if path.name == 'eeMemory.bin' else path.name,
                        'line': hex(current), 'draws': draws, 'calls': calls})
    report['captured_resumes'] = resumes
    report['fault_fixture_checks'] = fault_fixture()
    report['boundaries'] = ('001CBE10 glyph advance (answers from the executed original) and 001FC7B0 '
                            'glyph-run draw are workers; glyph pixels are not compared here')
    out = ROOT / 'build/message_draw_reference'
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
