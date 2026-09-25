#!/usr/bin/env python3
"""Execute the original cue line walker 001FDDB0 (and the cue presenter
001FD0E0 around it) and compare src/game/em_message_presenter_rest.c; check
the presenters' exported data against the captures.
docs/MESSAGE_PRESENTER_REST.md.

The user's pinned ELF and the captured EE RAM (../Extermination/build/
startup-reference and the s87 route beats 00..14) supply every instruction,
table, bank and record; none are embedded here. The report in build/ holds
addresses, counts and differences only.

A. Export. tools/export_message_data.py runs into build/: its .emmd must be
   byte-identical to the existing assets/message/message_data.emmd, and
   every field of its .emmp (the help / record containers, the cue bank, the
   D_00264CF0 / D_00264C90 configs, the D_00275830 / D_00275820 styles,
   D_00264DB0, &D_00264C90) must equal the bytes at the original's address
   in every capture. em_mpr_data_parse reads the file back field by field
   and refuses malformed images.
B. 001FDDB0 on its own: hand-made slot tables over records of this file's
   own (tags 0, 1, 2, 3, 4, 7, 0x20; every gate mode; counts -1 .. 0x46) and
   a random sweep. 00121A28, 001FC770 and 001CC170 execute original
   instructions; the glyph advance 001CBE10 and the run draw 001FC7B0 are
   hooked as in tools/test_message_draw_reference.py; the sound 001FB9F0 is
   hooked. Every draw / advance / sound call, the result, every block byte,
   the style D_00275C50..57 and D_00820ED0..D_00821010 are compared, and
   every store of the original must land on a modelled byte.
C. 001FD0E0 with 001FDDB0 executing on the original side and the native
   em_cs_001FD0E0 bound to em_mpr_worker_line_draw over the data parsed from
   the exported .emmp: the real cue bank's lines, frame by frame (state 0,
   then state 1 until the presenter marks the page done). Both outcomes of
   every conditional branch of 001FDDB0 are asserted reached over B and C.
D. Fail-stop: a NULL sound worker, a record outside the cue bank, a colour
   index outside D_0026EC10, a block +0x20 naming no config, a latched fault.

Default run (~10 s): every case class, the boundary cases, one real cue
line to the end and the other lines' first frames. EM_TEST_FULL=1: every
real cue line to the end and a longer random sweep.
"""
import ctypes as C
import json
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM  # noqa: E402
import test_census_standins_reference as cs  # noqa: E402
import test_message_draw_reference as md  # noqa: E402
from test_player_slide_reference import read_elf, s32, STACK_TOP  # noqa: E402

DECOMP = cs.DECOMP
OUT = ROOT / 'build' / 'message_presenter_rest_reference'
M32 = 0xFFFFFFFF
WALK, WALK_SIZE, SOUND = 0x1FDDB0, 0x2B8, 0x1FB9F0
CUE = cs.CUE
BLOCK = cs.BLOCK
REC, SLOTS, LINE = 0x01E00000, 0x01E10000, 0x01E20000
CFG_CF0, CFG_C90, D264DB0 = cs.CFG_CF0, cs.CFG_C90, cs.D264DB0
STYLE_830, STYLE_820 = 0x275830, 0x275820
RUN = 0x820F50

VP, I32, U32 = C.c_void_p, C.c_int32, C.c_uint32
SOUND_FN = C.CFUNCTYPE(C.c_int, VP, I32, I32, I32, I32)


class LineDraw(C.Structure):
    _fields_ = [('draw', C.POINTER(md.Draw)), ('data', C.POINTER(cs.CsData)), ('context', VP),
                ('sound', SOUND_FN), ('fault', C.c_char_p)]


class MprData(C.Structure):
    _fields_ = [('cs', cs.CsData), ('config_264CF0', md.Config), ('config_264C90', md.Config),
                ('style_275830', md.Style), ('style_275820', md.Style), ('owned', VP)]


def u32(buf, at):
    return struct.unpack_from('<I', buf, at)[0]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    lib = OUT / f'message_presenter_rest.{ext}'
    shared = '-dynamiclib' if sys.platform == 'darwin' else '-shared'
    sources = ['src/game/em_message_presenter_rest.c', 'src/game/em_census_standins.c',
               'src/game/em_effect_original.c', 'src/game/em_owner_services_original.c',
               'src/game/em_render_verify_rest.c', 'src/game/em_sdk_soft_float.c',
               'src/game/em_message_draw_original.c']
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-fPIC', shared,
                    '-Isrc'] + sources + ['-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    P, D, CD = C.POINTER(cs.Presenters), C.POINTER(md.Draw), C.POINTER(cs.CsData)
    n.em_cs_presenters_init.argtypes = [P, D, CD, C.POINTER(cs.Mode3), C.POINTER(I32)]
    n.em_cs_001FD0E0.argtypes = [P, VP]
    n.em_message_draw_init.argtypes = [D, C.POINTER(md.Data), C.POINTER(md.Workers)]
    n.em_mpr_line_draw_init.argtypes = [C.POINTER(LineDraw), D, CD, VP, SOUND_FN]
    n.em_mpr_001FDDB0.argtypes = [C.POINTER(LineDraw), VP, I32, VP, VP, C.POINTER(I32)]
    n.em_mpr_data_parse.argtypes = [C.POINTER(MprData), VP, C.c_size_t, U32]
    n.em_mpr_data_load.argtypes = [C.POINTER(MprData), C.c_char_p, U32]
    n.em_mpr_data_free.argtypes = [C.POINTER(MprData)]
    n.em_mpr_data_free.restype = None
    return n


def check_callees(elf):
    e = cs.EE(elf)
    targets = cs.rvr.call_targets(e, WALK, WALK_SIZE)
    assert targets == {0x121A28, 0x1FC770, 0x1CC170, SOUND}, sorted(map(hex, targets))
    return len(targets)


# ======================================================================
# A. Export
# ======================================================================

def section_a(lib):
    emmd, emmp = OUT / 'message_data.emmd', OUT / 'message_presenters.emmp'
    subprocess.run([sys.executable, str(ROOT / 'tools/export_message_data.py'), '--out', str(emmd),
                    '--presenters-out', str(emmp)], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    stats = {}
    existing = ROOT / 'assets/message/message_data.emmd'
    if existing.exists():
        assert emmd.read_bytes() == existing.read_bytes(), 'message_data.emmd changed'
        stats['emmd_identical'] = True
    image = emmp.read_bytes()
    assert image[:4] == b'EMMP' and u32(image, 4) == 1
    sizes = [u32(image, 8 + 4 * i) for i in range(3)]
    assert 104 + sum(sizes) == len(image)
    parts = {}
    at = 104
    for name, size in zip(('help', 'records', 'cue'), sizes):
        parts[name] = image[at:at + size]
        at += size
    assert u32(image, 20) == CFG_C90
    stats['sizes'] = {k: hex(len(v)) for k, v in parts.items()}
    captures = 0
    for label, path, _ in cs.captures():
        ram = path.read_bytes()
        for name, cell in (('help', cs.HELP_PTR), ('records', cs.REC_PTR), ('cue', cs.CUE_PTR)):
            a = u32(ram, cell)
            assert ram[a:a + len(parts[name])] == parts[name], (label, name)
        assert image[24:44] == ram[CFG_CF0:CFG_CF0 + 20] and image[44:64] == ram[CFG_C90:CFG_C90 + 20], label
        assert u32(ram, CFG_CF0 + 0x14) == STYLE_830 and u32(ram, CFG_C90 + 0x14) == STYLE_820, label
        assert image[64:72] == ram[STYLE_830:STYLE_830 + 8] and image[72:80] == ram[STYLE_820:STYLE_820 + 8], label
        assert image[80:104] == ram[D264DB0:D264DB0 + 24], label
        # The cue bank's extent is the extent the message-draw oracle computes.
        assert len(parts['cue']) == md.bank_extent(ram, u32(ram, cs.CUE_PTR)), label
        captures += 1
    stats['captures'] = captures
    # Native parse, field by field, and refusals.
    buf = C.create_string_buffer(image, len(image))
    d = MprData()
    assert lib.em_mpr_data_parse(C.byref(d), buf, len(image), 0x11739C0) == 1
    base = C.addressof(buf)
    for name, off, size in (('help', 104, sizes[0]), ('records', 104 + sizes[0], sizes[1]),
                            ('cue', 104 + sizes[0] + sizes[1], sizes[2])):
        bank = getattr(d.cs, name)
        assert bank.bytes == base + off and bank.size == size, name
    assert d.cs.cue_address == 0x11739C0 and d.cs.config_264C90_address == CFG_C90
    assert list(d.config_264CF0.word) == list(struct.unpack_from('<5i', image, 24))
    assert list(d.config_264C90.word) == list(struct.unpack_from('<5i', image, 44))
    assert bytes(d.style_275830) == image[64:72] and bytes(d.style_275820) == image[72:80]
    assert C.addressof(d.config_264CF0.style.contents) == C.addressof(d.style_275830)
    assert C.addressof(d.config_264C90.style.contents) == C.addressof(d.style_275820)
    assert C.addressof(d.cs.config_264CF0.contents) == C.addressof(d.config_264CF0)
    assert list(d.cs.d264DB0) == list(struct.unpack_from('<6I', image, 80))
    loaded = MprData()
    assert lib.em_mpr_data_load(C.byref(loaded), str(emmp).encode(), 7) == 1
    assert C.string_at(loaded.cs.cue.bytes, sizes[2]) == parts['cue'] and loaded.cs.cue_address == 7
    lib.em_mpr_data_free(C.byref(loaded))
    refused = 0
    for bad in (b'EMMQ' + image[4:], image[:4] + struct.pack('<I', 2) + image[8:], image[:-1], image + b'\0',
                image[:103]):
        b2 = C.create_string_buffer(bad, len(bad))
        assert lib.em_mpr_data_parse(C.byref(d), b2, len(bad), 0) == 0 and not d.cs.help.bytes
        refused += 1
    stats['refused'] = refused
    return stats, image


# ======================================================================
# B. 001FDDB0 on its own
# ======================================================================

class WalkOriginal(md.Original):
    """md.Original plus the sound hook and the branch outcomes of 001FDDB0."""

    def __init__(self, elf, ram, advances, outcomes):
        super().__init__(elf, ram, None, advances)
        ee = self.ee
        branch = ee.branch

        def recording(word, pc):
            b = branch(word, pc)
            unconditional = word >> 26 == 4 and (word >> 16 & 0x3FF) == 0
            if b is not None and not unconditional and WALK <= pc < WALK + WALK_SIZE:
                outcomes.add((pc, b[0]))
            return b
        ee.branch = recording
        ee.hooks[SOUND] = self.sound

    def sound(self, e):
        self.log.append(('sound', s32(e.arg(0)), s32(e.arg(1)), s32(e.arg(2)), s32(e.arg(3))))
        e.ret_int(0)


class WalkBench:
    def __init__(self, elf, lib, ram, advances, outcomes):
        self.lib, self.ram = lib, bytes(ram)
        self.o = WalkOriginal(elf, self.ram, advances, outcomes)
        self.n = md.Native(lib, self.ram, advances, {})
        self.style820 = md.Style.from_buffer_copy(self.ram[STYLE_820:STYLE_820 + 8])
        self.cfg = md.Config((I32 * 5)(*struct.unpack_from('<5i', self.ram, CFG_C90)), C.pointer(self.style820))
        self.recs = C.create_string_buffer(0x400)
        self.data = cs.CsData(md.Bank(), md.Bank(), md.Bank(C.cast(self.recs, VP), 0x400), REC, None,
                              C.pointer(self.cfg), CFG_C90, (U32 * 6)())
        self.sound_cb = SOUND_FN(self.sound)
        self.ld = LineDraw()
        assert lib.em_mpr_line_draw_init(C.byref(self.ld), C.byref(self.n.draw_state), C.byref(self.data),
                                         None, self.sound_cb) == 1
        self.block = (C.c_uint8 * 0x9C)(*self.ram[BLOCK:BLOCK + 0x9C])

    def sound(self, _, a0, a1, a2, a3):
        self.n.log.append(('sound', a0, a1, a2, a3))
        return 0

    def run(self, records, slots, count, line, words, where):
        rec_bytes = b''.join(struct.pack('<4I', *(v & M32 for v in r)) for r in records).ljust(0x400, b'\0')
        slot_words = [0] * 64
        for index, r in slots.items():
            slot_words[index] = REC + 16 * r
        slot_bytes = struct.pack('<64I', *slot_words)
        line_bytes = line.ljust(0x40, b'\0')[:0x40]
        block = bytearray(self.ram[BLOCK:BLOCK + 0x9C])
        struct.pack_into('<I', block, 0x20, CFG_C90)
        struct.pack_into('<I', block, 0x2C, LINE)
        for off, value in words.items():
            struct.pack_into('<I', block, off, value & M32)
        ee = self.o.ee
        ee.write(REC, rec_bytes)
        ee.write(SLOTS, slot_bytes)
        ee.write(LINE, line_bytes)
        ee.write(BLOCK, bytes(block))
        ee.r[29] = STACK_TOP
        want = self.o.call(WALK, SLOTS, count, BLOCK)
        C.memmove(self.recs, rec_bytes, 0x400)
        C.memmove(self.block, bytes(block), 0x9C)
        sl = C.create_string_buffer(slot_bytes, 0x100)
        ln = C.create_string_buffer(line_bytes, 0x40)
        got = I32(0x5A5A5A5A)
        r = self.lib.em_mpr_001FDDB0(C.byref(self.ld), sl, count, ln, self.block, C.byref(got))
        assert r == 0, (where, self.ld.fault)
        assert got.value == want, (where, 'result', got.value, want)
        assert bytes(self.block) == ee.read(BLOCK, 0x9C), (where, 'block', [
            hex(i) for i in range(0x9C) if self.block[i] != ee.read(BLOCK, 0x9C)[i]])
        calls = md.same(self.o, self.n, where)
        assert not self.o.unmodelled(range(BLOCK, BLOCK + 0x9C)), (where, [hex(a) for a in self.o.unmodelled()[:8]])
        self.o.stores.clear()
        return calls


ABC = b'ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789.,'


def walk_cases():
    """(records, {slot: record}, count, line, block words, label)."""
    R = [(3, 1, 0, 0), (2, 3, 0, 0), (4, 5, 0, 2), (0, 9, 0, 0), (7, 2, 0, 0), (0x20, 0, 0, 0),
         (1, 3, 5, 0), (1, 0, 5, 0), (3, 0x3F, 0, 0), (4, 15, 0, 0x21), (2, 0, 0, 0), (1, 2, 0xFFFFFFFF, 0)]
    g = {0x40: 0, 0x48: 0, 0x58: 0, 0x24: 0x37, 0x28: 0x1C}
    cases = [
        (R, {}, 5, b'HELLO', g, 'no records'),
        (R, {0: 0}, -1, b'HELLO', g, 'count -1'),
        (R, {0: 0}, 0, b'HELLO', g, 'count 0 record at 0'),
        (R, {0: 0, 3: 1, 6: 2}, 10, b'ABCDEFGHIJ', g, 'tags 3 2 4'),
        (R, {2: 3, 4: 4}, 8, b'TAG ZERO', g, 'tags 0 7'),
        (R, {3: 5}, 9, b'ABORT ME!', g, 'tag 0x20'),
        (R, {1: 0, 3: 5, 5: 0}, 9, b'ABORT ME!', g, 'tag 0x20 after a draw'),
        (R, {0: 0, 2: 6}, 9, b'GATE ARMS', g, 'gate arms'),
        (R, {0: 0, 2: 6}, 9, b'GATE WAIT', {**g, **{0x48: 1, 0x58: 3, 0x40: 5}}, 'gate counts'),
        (R, {0: 0, 2: 6, 5: 1}, 9, b'GATE DONE', {**g, **{0x48: 1, 0x58: 1, 0x40: 5}}, 'gate finishes'),
        (R, {0: 0, 2: 7, 5: 1}, 9, b'GATE ZERO', g, 'gate zero count'),
        (R, {2: 6}, 9, b'GATE MODE', {**g, **{0x48: 2}}, 'gate mode 2'),
        (R, {2: 6}, 9, b'GATE MODE', {**g, **{0x48: 0xFFFFFFFF}}, 'gate mode -1'),
        (R, {2: 6, 4: 2}, 9, b'GATE PASS', {**g, **{0x40: 9}}, 'gate passed'),
        (R, {2: 6}, 9, b'GATE EDGE', {**g, **{0x40: 5}}, 'gate +0x40 == trigger'),
        (R, {2: 6}, 9, b'GATE HIGH', {**g, **{0x40: 0xFFFFFFFF}}, 'gate +0x40 unsigned'),
        (R, {2: 11}, 9, b'GATE HIGH', {**g, **{0x40: 0xFFFFFFFF}}, 'gate trigger -1'),
        (R, {2: 6}, 9, b'GATE MINUS', {**g, **{0x48: 1, 0x58: 0}}, 'gate count 0 -> -1'),
        (R, {2: 6}, 9, b'GATE WRAP', {**g, **{0x48: 1, 0x58: 0x80000000}}, 'gate count wraps'),
        (R, {3: 0, 4: 1, 5: 2}, 9, b'IN A ROW!', g, 'consecutive records'),
        (R, {0: 8, 1: 9, 2: 10}, 6, b'FLAGS!', g, 'byte shifts and colours'),
        (R, {1: 0, 63: 1}, 70, ABC, g, 'count past the 0x40 cap'),
        (R, {63: 1}, 63, ABC, g, 'record at slot 63'),
        (R, {10: 0}, 0x40, ABC, g, 'count 0x40'),
        (R, {1: 0, 4: 0}, 6, b'\x01\x1f\x7f\x80\xff ', g, 'low and high bytes'),
        (R, {1: 0, 4: 0}, 6, b'A\0B\0C\0', g, 'NUL glyphs'),
        (R, {1: 0, 4: 0}, 6, b'NOCFG!', {**g, **{0x20: 0}}, 'config 0: template'),
        (R, {1: 0, 4: 0}, 6, b'PEN XY', {**g, **{0x24: 0xFFFFFFF0, 0x28: 0x7FFFFFFF}}, 'pen extremes'),
        (R, {0: 6, 7: 6, 9: 2}, 12, b'TWO GATES...', {**g, **{0x40: 5}}, 'repeated gate record'),
    ]
    return cases


def random_cases(rng, count):
    tags = (0, 1, 1, 2, 3, 4, 7, 0x20)
    cases = []
    for k in range(count):
        records = []
        for _ in range(12):
            tag = rng.choice(tags)
            arg = rng.randrange(16) if tag in (2, 4) else rng.choice((0, 1, 2, 5, rng.getrandbits(32)))
            records.append((tag, arg, rng.choice((0, 1, 3, 7, 20, rng.getrandbits(32))), rng.getrandbits(8)))
        n = rng.choice((0, 1, 5, 20, 40, 63, 64, 70))
        slots = {rng.randrange(0, 64): rng.randrange(12) for _ in range(rng.randrange(0, 8))}
        line = bytes(rng.randrange(0x20, 0x7F) for _ in range(64))
        words = {0x40: rng.choice((0, 1, 5, 20, rng.getrandbits(32))), 0x48: rng.choice((0, 0, 1, 1, 2)),
                 0x58: rng.choice((0, 1, 2, 9, rng.getrandbits(32))), 0x24: rng.randrange(-40, 400),
                 0x28: rng.randrange(-40, 400)}
        cases.append((records, slots, n, line, words, ('random', k)))
    return cases


def section_b(elf, lib, ram, advances, rng, outcomes):
    b = WalkBench(elf, lib, ram, advances, outcomes)
    stats = {'cases': 0, 'random': 0, 'calls': 0}
    for records, slots, count, line, words, label in walk_cases():
        stats['calls'] += b.run(records, slots, count, line, words, label)
        stats['cases'] += 1
    for records, slots, count, line, words, label in random_cases(rng, RM.pick(2000, 120)):
        stats['calls'] += b.run(records, slots, count, line, words, label)
        stats['random'] += 1
    return stats


# ======================================================================
# C. 001FD0E0 around 001FDDB0 over the real cue bank
# ======================================================================

class CueBench(cs.Bench):
    """cs.Bench with 001FDDB0 executing on the original side and the native
    presenters bound to em_mpr_worker_line_draw over the exported data."""

    def __init__(self, elf, lib, ram, advances, image, outcomes):
        super().__init__(elf, lib, ram, None, advances)
        ee = self.o.ee
        ee.hooks.pop(cs.LINE_DRAW)
        ee.hooks[SOUND] = self.o_sound
        branch = ee.branch

        def recording(word, pc):
            b = branch(word, pc)
            unconditional = word >> 26 == 4 and (word >> 16 & 0x3FF) == 0
            if b is not None and not unconditional and WALK <= pc < WALK + WALK_SIZE:
                outcomes.add((pc, b[0]))
            return b
        ee.branch = recording
        self.image = C.create_string_buffer(image, len(image))
        self.mpr = MprData()
        assert lib.em_mpr_data_parse(C.byref(self.mpr), self.image, len(image), self.addresses['cue']) == 1
        self.sound_cb = SOUND_FN(self.n_sound)
        self.ld = LineDraw()
        assert lib.em_mpr_line_draw_init(C.byref(self.ld), C.byref(self.n.draw_state), C.byref(self.mpr.cs),
                                         None, self.sound_cb) == 1
        d282228 = C.cast(C.addressof(self.block) + 0x78, C.POINTER(I32))
        assert lib.em_cs_presenters_init(C.byref(self.p), C.byref(self.n.draw_state), C.byref(self.mpr.cs),
                                         C.byref(self.mode3), d282228) == 1
        self.p.d820EC0[:] = struct.unpack_from('<4I', self.ram, cs.D820EC0)
        self.p.frame_address = STACK_TOP - 0x860 + 0xC0
        self.p.mode3.line_draw = C.cast(lib.em_mpr_worker_line_draw, cs.LINE_FN)
        self.p.mode3.context = C.addressof(self.ld)

    def o_sound(self, e):
        self.o.log.append(('sound', s32(e.arg(0)), s32(e.arg(1)), s32(e.arg(2)), s32(e.arg(3))))
        e.ret_int(0)

    def n_sound(self, _, a0, a1, a2, a3):
        self.n.log.append(('sound', a0, a1, a2, a3))
        return 0

    def frame(self, where):
        ee = self.o.ee
        ee.r[29] = STACK_TOP
        self.o.call(CUE, BLOCK, 2)
        assert self.lib.em_cs_001FD0E0(C.byref(self.p), self.block) == 0, (where, self.p.fault, self.ld.fault)
        want = ee.read(BLOCK, 0x9C)
        assert bytes(self.block) == want, (where, 'block', [
            hex(i) for i in range(0x9C) if self.block[i] != want[i]])
        assert list(self.p.d820EC0) == [ee.load(cs.D820EC0 + 4 * i) for i in range(4)], (where, 'D_00820EC0')
        return self.same(where, extra=list(range(BLOCK, BLOCK + 0x9C)) + list(range(cs.D820EC0, cs.D820EC0 + 0x10)))


def cue_line(args):
    """One real cue line from state 0 until the presenter marks the page
    done (block +0x10), or `limit` frames."""
    line, limit = args
    outcomes = set()
    b = CueBench(G['elf'], G['lib'], G['ram'], G['advances'], G['image'], outcomes)
    block = bytearray(b.ram[BLOCK:BLOCK + 0x9C])
    for off, value in ((0x78, 0), (0x08, line), (0x34, line), (0x38, 0), (0x40, 0), (0x44, 0), (0x48, 0),
                       (0x58, 0), (0x10, 0)):
        struct.pack_into('<I', block, off, value)
    b.o.ee.write(BLOCK, bytes(block))
    C.memmove(b.block, bytes(block), 0x9C)
    calls = frames = 0
    while frames < limit:
        calls += b.frame(('cue line', line, 'frame', frames))
        frames += 1
        if u32(bytes(b.block), 0x10) == 1:
            break
    done = u32(bytes(b.block), 0x10) == 1
    return {'line': line, 'frames': frames, 'done': done, 'calls': calls, 'progress': u32(bytes(b.block), 0x40),
            'outcomes': sorted(outcomes)}


G = {}


def section_c(elf, lib, ram, advances, image, outcomes):
    G.update(elf=elf, lib=lib, ram=ram, advances=advances, image=image)
    cue = u32(ram, cs.CUE_PTR)
    lines = [i for i in range(u32(ram, cue + 4)) if u32(ram, cue + 0x10 + 16 * i + 0xC) >> 4]
    # Quick: the shortest line to the end, the others' first frames.
    shortest = min(lines, key=lambda i: u32(ram, cue + 0x10 + 16 * i + 0xC))
    jobs = [(line, 100000 if RM.FULL or line == shortest else 12) for line in lines]
    results = RM.parallel_map(cue_line, jobs, cost=lambda j: j[1])
    for r in results:
        outcomes.update(tuple(o) for o in r.pop('outcomes'))
        full = RM.FULL or r['line'] == shortest
        assert r['done'] or not full, ('cue line never finished', r)
    return {'lines': results}


# ======================================================================
# D. Fail-stop
# ======================================================================

def section_d(elf, lib, ram, advances):
    checks = 0
    R = [(1, 3, 5, 0), (2, 16, 0, 0), (3, 1, 0, 0)]
    g = {0x40: 0, 0x48: 0, 0x58: 0}

    def native(b, records, slots, count, line, words, bad_slot=None):
        rec_bytes = b''.join(struct.pack('<4I', *r) for r in records).ljust(0x400, b'\0')
        C.memmove(b.recs, rec_bytes, 0x400)
        slot_words = [0] * 64
        for index, r in slots.items():
            slot_words[index] = REC + 16 * r
        if bad_slot is not None:
            slot_words[bad_slot[0]] = bad_slot[1]
        block = bytearray(b.ram[BLOCK:BLOCK + 0x9C])
        struct.pack_into('<I', block, 0x20, CFG_C90)
        for off, value in words.items():
            struct.pack_into('<I', block, off, value & M32)
        C.memmove(b.block, bytes(block), 0x9C)
        got = I32()
        r = lib.em_mpr_001FDDB0(C.byref(b.ld), C.create_string_buffer(struct.pack('<64I', *slot_words), 0x100),
                                count, C.create_string_buffer(line.ljust(0x40, b'\0'), 0x40), b.block,
                                C.byref(got))
        return r, bytes(b.block)

    # A NULL sound worker: the gate's stores are made, then the fault.
    b = WalkBench(elf, lib, ram, advances, set())
    b.ld.sound = SOUND_FN()
    r, block = native(b, R, {2: 0}, 5, b'ARMED', g)
    assert r == -1 and b.ld.fault == b'001FB9F0 worker missing'
    assert u32(block, 0x48) == 1 and u32(block, 0x58) == 3 and u32(block, 0x40) == 5
    assert [e[0] for e in b.n.log].count('draw') == 1
    checks += 1
    # The latched fault refuses the next call before any worker call.
    b.n.log.clear()
    r, _ = native(b, R, {}, 5, b'AGAIN', g)
    assert r == -1 and b.n.log == []
    checks += 1
    # A record outside the cue bank, a colour outside D_0026EC10, a block
    # +0x20 naming no config, an unaligned record.
    for label, slots, bad, words, why in (
            ('record outside', {}, (1, REC + 0x400), g, b'cue record outside the cue bank'),
            ('record unaligned', {}, (1, REC + 2), g, b'cue record outside the cue bank'),
            ('colour outside', {1: 1}, None, g, b'D_0026EC10 index outside table'),
            ('no config', {1: 2}, None, {**g, **{0x20: 0x264CF0}}, b'block +0x20 names no supplied config')):
        b = WalkBench(elf, lib, ram, advances, set())
        r, _ = native(b, R, slots, 5, b'FAULT', words, bad)
        assert r == -1 and b.ld.fault == why, (label, b.ld.fault)
        checks += 1
    # Unbound.
    ld = LineDraw()
    assert lib.em_mpr_line_draw_init(C.byref(ld), None, None, None, SOUND_FN()) == 0
    assert lib.em_mpr_001FDDB0(None, None, 0, None, None, None) == -1
    checks += 1
    return checks


# ======================================================================

def main():
    elf = read_elf()
    rng = random.Random(0xDDB0)
    lib = build_native()
    report = {'mode': RM.MODE, 'callees': check_callees(elf)}
    report['A_export'], image = section_a(lib)
    ram = (cs.REF / 'panel' / 'eeMemory.bin').read_bytes()
    advances = md.original_advances(elf, ram)
    outcomes = set()
    report['B_001FDDB0'] = section_b(elf, lib, ram, advances, rng, outcomes)
    report['C_001FD0E0'] = section_c(elf, lib, ram, advances, image, outcomes)
    sites = {pc for pc, _ in outcomes}
    one_sided = sorted(hex(pc) for pc in sites if len({t for p, t in outcomes if p == pc}) == 1)
    report['branch_sites'], report['one_sided'] = len(sites), one_sided
    assert len(sites) == 14 and not one_sided, ('001FDDB0 branch outcomes not reached', len(sites), one_sided)
    report['D_fail_stop'] = section_d(elf, lib, ram, advances)
    b, c = report['B_001FDDB0'], report['C_001FD0E0']['lines']
    RM.banner(f"001FDDB0 {b['cases']} cases + {b['random']} random ({b['calls']} calls)",
              '001FD0E0 cue lines ' + ' '.join(f"{r['line']}:{r['frames']}{'*' if r['done'] else ''}" for r in c),
              f"export {report['A_export']['captures']} captures")
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'result.json').write_text(json.dumps(report, indent=2, default=str) + '\n')
    print(json.dumps(report, default=str))
    print('PASS')


if __name__ == '__main__':
    main()
