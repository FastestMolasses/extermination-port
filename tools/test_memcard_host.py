#!/usr/bin/env python3
"""The host memory card's file commands (em_memcard) under the ORIGINAL save
and load code, and on their own.

docs/SAVE_GAME.md section 5. Two parts:

1. The original's code over the host card. The save writer 00228A90 runs
   frame by frame on both sides of test_save_reference's harness (the
   original instructions over the a01_s4 / opt_07 recordings' RAM, and the
   native em_save_* translations), with every libmc call answered by
   em_memcard itself on a host card folder of its own per side (instead of
   that test's scripted card model): at every callee entry RAM, the
   scratchpad and the arguments must equal the original's, the two host
   folders must be byte-identical, and the folder must hold the save layout
   docs/SAVE_GAME.md section 2 records (test_save_reference.check_card).
   Then the ORIGINAL load side runs over the RAM the save left: a second
   save into the existing directory (00228A90 again, another slot), and the
   card screen's load enumeration 002283B0 (GetDir of the directory, then
   00228EB0: GetDir of its files, the name check 00228C90, each EX_DATA
   slot opened, read and closed, the check byte 00228C50, GetInfo) until it
   reports 1. Its slot records must be the two saved blocks' headers, and
   only those slots present: the round trip, save then load, through the
   original's own code. Negative controls: a slot file with one byte changed,
   and a directory without icon.sys, make the original reject the card (3).

2. em_memcard directly (ctypes, a Python memory): every command's answer,
   the pending / Sync protocol, GetDir's listing, Read at the end of a
   file, and every refusal (a fault where no recording shows the server's
   answer).

The card server is the IOP's, not EE code: what em_memcard answers (a
descriptor 0..2, GetDir's entries, the counts) is checked here against what
the original EE code accepts and reads back, not against a recording of the
server; that recording is docs/SAVE_GAME.md section 8's next step (section
6.2's fork recording wrote no save: the game's Format failed on the fresh card).

Host folders live in a temporary directory removed at the end. Quick by
default (a covering set of saves); EM_TEST_FULL=1 runs every slot and port
on both recordings.
"""
import ctypes as C
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM  # noqa: E402
import test_save_reference as S  # noqa: E402
import test_area01_render_reference as R  # noqa: E402
import test_area01_ui_reference as U  # noqa: E402
from test_player_slide_reference import RETURN, STACK_TOP  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
MC = S.MC
OPEN, MKDIR, CLOSE, WRITE, GETDIR, SYNC = S.OPEN, S.MKDIR, S.CLOSE, S.WRITE, S.GETDIR, S.SYNC
GETINFO, READ = 0x114988, 0x114630
SDK_ARGS = {GETINFO: 5, SYNC: 3, OPEN: 4, MKDIR: 3, CLOSE: 1, WRITE: 3, READ: 3, GETDIR: 6}
LOAD = 0x2283B0                     # the card screen's load enumeration (state +2)
DIR = 'BASCUS-97112-DS00-00'
SLOTS = ['%s/EX_DATA.%02d' % (DIR, n) for n in range(5)]

READF = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_void_p, C.c_uint32)
WRITEF = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_void_p, C.c_uint32)


class Memory(C.Structure):
    _fields_ = [('ctx', C.c_void_p), ('read', READF), ('write', WRITEF)]


LIB = None


def build_lib():
    out = S.OUT
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('memcard' + ('.dylib' if sys.platform == 'darwin' else '.so'))
    src = ['src/game/em_memcard.c']
    h = hashlib.sha256(repr(S.CFLAGS).encode())
    for q in src + ['src/game/em_memcard.h']:
        h.update((ROOT / q).read_bytes())
    stamp = lib.with_suffix(lib.suffix + '.sha256')
    if not (lib.exists() and stamp.exists() and stamp.read_text() == h.hexdigest()):
        subprocess.run(['cc'] + S.CFLAGS + src + ['-o', str(lib)], cwd=ROOT, check=True)
        stamp.write_text(h.hexdigest())
    m = C.CDLL(str(lib))
    m.em_memcard_call.argtypes = [C.c_uint32, C.POINTER(C.c_uint64), C.c_uint, C.POINTER(Memory),
                                  C.POINTER(C.c_uint64)]
    m.em_memcard_set_root.argtypes = [C.c_char_p]
    m.em_memcard_set_read_only.argtypes = [C.c_int]
    m.em_memcard_fault.restype = C.c_char_p
    m.em_memcard_is_sdk.argtypes = [C.c_uint32]
    return m


def fresh(root):
    """em_memcard back to the boot state on the host card folder `root`."""
    LIB.em_memcard_reset()
    LIB.em_memcard_set_root(str(root).encode())
    assert LIB.em_memcard_boot_check() == 1, root


def call(target, args, read, write):
    """One libmc call through em_memcard_call; (rc, v0, fault text)."""
    keep = (READF(lambda _c, a, dst, n: read(a, dst, n)), WRITEF(lambda _c, a, src, n: write(a, src, n)))
    mem = Memory(None, *keep)
    regs = (C.c_uint64 * 6)(*[(x & MASK64) for x in args] + [0] * (6 - len(args)))
    v0 = C.c_uint64(0)
    rc = LIB.em_memcard_call(target, regs, len(args), C.byref(mem), C.byref(v0))
    fault = LIB.em_memcard_fault()
    return rc, v0.value, fault.decode() if fault else None


def ram_access(load_bytes, store_byte):
    """read / write callbacks over an EE memory (RAM below 32 MiB)."""
    def read(a, dst, n):
        if not (a < 0x40000000 and (a & 0x1FFFFFF) + n <= R.RAM_SIZE):
            return -1
        C.memmove(dst, load_bytes(a & 0x1FFFFFF, n), n)
        return 0

    def write(a, src, n):
        if not (a < 0x40000000 and (a & 0x1FFFFFF) + n <= R.RAM_SIZE):
            return -1
        data = C.string_at(src, n)
        for i, b in enumerate(data):
            store_byte((a & 0x1FFFFFF) + i, b)
        return 0
    return read, write


def card_files(root, port):
    base = Path(root) / ('slot%d' % (port + 1))
    out = {}
    for p in sorted(base.rglob('*')):
        if p.is_file() and not p.is_symlink():
            out[str(p.relative_to(base))] = p.read_bytes()
    return out


# ---- part 1: the save on both sides of the harness, over em_memcard -------

class HostCard:
    """test_save_reference's Card interface, answered by em_memcard on a host
    folder of its own; `order` logs the commands issued (as that model's)."""
    roots = []          # the folder for each Card made, in order (oracle, native)
    dirs = ()           # card directories present before the case

    def __init__(self, _cfg):
        root = HostCard.roots.pop(0)
        fresh(root)
        for d in HostCard.dirs:
            for port in (0, 1):
                (Path(root) / ('slot%d' % (port + 1)) / d).mkdir()
        self.order = []

    def answer(self, mem, save, address, args):
        read, write = ram_access(lambda a, n: bytes(mem[a:a + n]), lambda a, b: save(a, b, 1))
        rc, v0, fault = call(address, args, read, write)
        assert rc == 0, ('em_memcard faulted', hex(address), fault)
        if address != SYNC and v0 == 0:
            name = S.cstr(mem, args[2]) if address in (OPEN, MKDIR, GETDIR) else None
            if address == OPEN:
                self.order.append(('open', name, args[3] & MASK))
            elif address in (MKDIR, GETDIR):
                self.order.append(('mkdir' if address == MKDIR else 'getdir', name))
            else:
                self.order.append((hex(address),))
        return v0


class Pseudo:
    def __init__(self, order, files):
        self.order, self.files = order, files


def save_both(tmp, tag, beat, chosen, port, exists):
    """One whole save, both sides of the harness, each on its own host card.
    Returns (the oracle's end RAM, scratchpad, the start RAM, its folder)."""
    roots = [Path(tmp) / tag / side for side in ('oracle', 'native')]
    for r in roots:
        r.mkdir(parents=True)
    HostCard.roots, HostCard.dirs = list(roots), ((DIR,) if exists else ())
    item = ('host-save', beat, {'chosen': chosen, 'port': port, 'exists': exists, 'card': {}})
    b = S.rec(port, chosen, state=0) + S.helpers() + [(MC + 0x50, 0, 4), (MC + 0x54, 0, 4)]
    b += [(MC + 0x6C + n, 0, 1) for n in range(5)] + [(MC + 0x74 + i, 0, 1) for i in range(60)]
    ram, spad = S.make(beat, {'b': b})
    start = bytes(ram)
    steps = [('call', 0x228A90, (MC,))] * S.SAVE_FRAMES
    S.run_both(item, bytes(spad), steps, check=lambda ee: ee.load(MC + 3, 1) == 3)
    W = R.work()
    end_ram, end_spad = bytes(W.oracle.data), bytes(W.oracle_live.spad)
    o, n = card_files(roots[0], port), card_files(roots[1], port)
    assert o == n, (item, 'the two sides wrote different host cards', sorted(o), sorted(n))
    assert not card_files(roots[0], 1 - port), (item, 'the other port was written')
    oracle_card = S.MODELS['oracle']
    assert oracle_card.order == S.MODELS['native'].order, (item, 'command order differs')
    S.check_card(item, Pseudo(oracle_card.order, o), start)
    return end_ram, end_spad, start, roots[0]


# ---- part 1b: the original's load side over the host card ------------------

class LoadEE(U.UIEE):
    """The UI lane's interpreter with PSUBW, which the string compare 00123020
    (00228C90's name check) runs: four 32-bit lanes, each a - b mod 2^32."""

    def mmi(self, word, pc):
        if (word & 63) == 0x08 and (word >> 6 & 31) == 0x01:                    # psubw
            rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
            a = (self.r[rs] & MASK64) | (self.rh[rs] << 64)
            b = (self.r[rt] & MASK64) | (self.rh[rt] << 64)
            value = 0
            for i in range(4):
                value |= ((a >> 32 * i) - (b >> 32 * i) & MASK) << 32 * i
            if rd:
                self.r[rd], self.rh[rd] = value & MASK64, value >> 64
            return
        return U.UIEE.mmi(self, word, pc)


def original_ee(ram, spad, root, reset=True):
    ee = LoadEE(S.ELF, b'', spad, journal=False)
    ee.mem = bytearray(ram)
    if reset:
        fresh(root)

    def hook(e, address):
        def store(a, b):
            e.mem[a] = b
        read, write = ram_access(lambda a, n: bytes(e.mem[a:a + n]), store)
        args = [e.r[4 + i] & MASK64 for i in range(SDK_ARGS[address])]
        rc, v0, fault = call(address, args, read, write)
        if rc != 0:
            raise AssertionError(('em_memcard faulted', hex(address), fault))
        e.calls.append((address, tuple(a & MASK for a in args), v0 & MASK))
        e.r[2] = v0 & MASK64
    ee.hooks = {a: (lambda e, a=a: hook(e, a)) for a in SDK_ARGS}
    return ee


def run_frames(ee, entry, frames):
    for f in range(frames):
        ee.r[4], ee.r[29], ee.r[31] = MC, STACK_TOP, RETURN
        ee.run(entry)
        v = ee.r[2] & MASK
        if v:
            return v, f + 1
    return 0, frames


def setup_record(ee, port, chosen=0):
    for at, v, size in S.rec(port, chosen, state=0) + S.helpers() + [(MC + 0x50, 0, 4), (MC + 0x54, 0, 4)]:
        ee.save(at, v, size)
    for at in (MC + 2, MC + 5, MC + 6):
        ee.save(at, 0, 1)
    ee.save(0x275844, MASK, 4)


def original_save(ee, port, chosen):
    setup_record(ee, port, chosen)
    v, frames = run_frames(ee, 0x228A90, S.SAVE_FRAMES)
    assert v == 1 and ee.load(MC + 3, 1) == 3, ('the original second save did not finish', v, frames)


def original_load(ee, port):
    setup_record(ee, port)
    for n in range(5):
        ee.save(MC + 0x6C + n, 0xEE, 1)
    return run_frames(ee, LOAD, 400)


def check_loaded(ee, files, present, where):
    got = [ee.load(MC + 0x6C + n, 1) for n in range(5)]
    assert got == [1 if n in present else 0 for n in range(5)], (where, 'slots present', got, present)
    assert ee.load(MC + 0x68) == len(present), (where, 'count')
    for n in present:
        blk = files[SLOTS[n]]
        rec = bytes(ee.mem[MC + 0x74 + 12 * n:MC + 0x80 + 12 * n])
        want = blk[0:6] + blk[0xA:0xB] + rec[7:8] + blk[0x50:0x54]
        assert rec == want, (where, 'slot record', n, rec.hex(), want.hex())


def round_trip(tmp, tag, beat, first, second, port):
    end_ram, end_spad, start, root = save_both(tmp, tag, beat, first, port, False)
    ee = original_ee(end_ram, end_spad, root)
    original_save(ee, port, second)
    files = card_files(root, port)
    want = sorted([DIR + '/' + DIR, DIR + '/EX.ico', DIR + '/icon.sys'] + SLOTS)
    assert sorted(files) == want, (tag, sorted(files))
    v, frames = original_load(ee, port)
    assert v == 1, (tag, 'the original load enumeration did not accept the card', v, frames)
    check_loaded(ee, files, {first, second}, tag)
    assert bytes(ee.mem[0x822050:0x822050 + 0x640]) == files[SLOTS[4]], (tag, 'the last slot read')
    names = [bytes(ee.mem[0x821500 + 0x40 * i + 0x20:0x821500 + 0x40 * i + 0x40]).split(b'\0')[0]
             for i in range(10)]
    assert sorted(names) == sorted([b'.', b'..', DIR.encode(), b'EX.ico', b'icon.sys'] +
                                   [s.split('/')[1].encode() for s in SLOTS]), (tag, names)
    calls = [c[0] for c in ee.calls]
    reads = [c for c in ee.calls if c[0] == READ]
    assert len(reads) == 5 and all(c[1][2] == 0x640 and c[2] == 0 for c in reads), (tag, reads)
    assert GETINFO in calls, tag
    # negative controls: the original rejects a changed slot and a missing icon.sys
    slot = Path(root) / ('slot%d' % (port + 1)) / SLOTS[second]
    data = bytearray(slot.read_bytes())
    data[0x100] ^= 1
    slot.write_bytes(bytes(data))
    bad = original_ee(end_ram, end_spad, root)
    assert original_load(bad, port)[0] == 3, (tag, 'a changed slot was accepted')
    data[0x100] ^= 1
    slot.write_bytes(bytes(data))
    icon = Path(root) / ('slot%d' % (port + 1)) / DIR / 'icon.sys'
    kept = icon.read_bytes()
    icon.unlink()
    bad = original_ee(end_ram, end_spad, root)
    assert original_load(bad, port)[0] == 3, (tag, 'a directory without icon.sys was accepted')
    icon.write_bytes(kept)
    again = original_ee(end_ram, end_spad, root)
    assert original_load(again, port)[0] == 1, (tag, 'restored card')
    return len(ee.calls)


# ---- part 2: em_memcard on its own -----------------------------------------

class Mem:
    def __init__(self):
        self.ram = bytearray(0x10000)

    def access(self):
        def read(a, dst, n):
            if a + n > len(self.ram):
                return -1
            C.memmove(dst, bytes(self.ram[a:a + n]), n)
            return 0

        def write(a, src, n):
            if a + n > len(self.ram):
                return -1
            self.ram[a:a + n] = C.string_at(src, n)
            return 0
        return read, write

    def put(self, at, data):
        self.ram[at:at + len(data)] = data

    def word(self, at):
        return struct.unpack_from('<i', self.ram, at)[0]


NAME, BUF, TABLE, CMD, RES, TYPE = 0x100, 0x1000, 0x4000, 0x80, 0x84, 0x88
FREE_WORD, FORMAT_WORD = 0x8C, 0x90


def unit(tmp):
    m = Mem()
    rd, wr = m.access()
    n = [0]

    def do(target, *args):
        rc, v0, fault = call(target, args, rd, wr)
        assert rc == 0, (hex(target), args, fault)
        n[0] += 1
        return v0 if v0 < 1 << 63 else v0 - (1 << 64)

    def sync():
        assert do(SYNC, 1, CMD, RES) == 1
        return m.word(CMD), m.word(RES)

    def name(text):
        m.put(NAME, text.encode() + b'\0')
        return NAME

    def refused(target, args, why, prepare=None):
        root = Path(tmp) / ('refuse%d' % n[0])
        root.mkdir()
        fresh(root)
        if prepare:
            prepare(root)
        rc, _, fault = call(target, args, rd, wr)
        n[0] += 1
        assert rc == -1 and fault and why in fault, (hex(target), args, fault)

    root = Path(tmp) / 'unit'
    root.mkdir()
    fresh(root)
    card = root / 'slot1'
    assert all(LIB.em_memcard_is_sdk(a) for a in SDK_ARGS) and not LIB.em_memcard_is_sdk(0x114510)
    assert do(SYNC, 1, CMD, RES) == -1                       # nothing pending
    assert do(GETDIR, 0, 0, name(DIR), 0, 1, TABLE) == 0
    assert sync() == (0xD, 0)                                # a fresh card: no directory
    assert do(MKDIR, 0, 0, name(DIR)) == 0
    assert do(OPEN, 0, 0, name(DIR + '/x'), 0x203) == 0xB    # pending: v0 its number, nothing served
    assert not (card / DIR / 'x').exists()
    assert sync() == (0xB, 0) and (card / DIR).is_dir()
    assert do(GETDIR, 0, 0, name(DIR), 0, 1, TABLE) == 0 and sync() == (0xD, 1)
    assert m.ram[TABLE + 0x20:TABLE + 0x40].rstrip(b'\0') == DIR.encode() and not any(m.ram[TABLE:TABLE + 0x20])
    payload = bytes((i * 7 + 3) & 0xFF for i in range(0x640))
    m.put(BUF, payload)
    assert do(OPEN, 0, 0, name(SLOTS[2]), 0x203) == 0 and sync() == (2, 0)     # descriptor 0
    assert do(OPEN, 0, 0, name(SLOTS[3]), 0x203) == 0 and sync() == (2, 1)     # descriptor 1
    assert do(WRITE, 0, BUF, 0x640) == 0 and sync() == (6, 0x640)
    assert do(WRITE, 0, BUF, 0x10) == 0 and sync() == (6, 0x10)                # at offset 0x640
    assert do(CLOSE, 0) == 0 and sync() == (3, 0)
    assert do(CLOSE, 1) == 0 and sync() == (3, 0)
    assert (card / SLOTS[2]).read_bytes() == payload + payload[:0x10]
    assert (card / SLOTS[3]).read_bytes() == b''
    # Open without truncation: a shorter write keeps the rest
    m.put(BUF, b'\xAA' * 4)
    assert do(OPEN, 0, 0, name(SLOTS[2]), 0x203) == 0 and sync() == (2, 0)
    assert do(WRITE, 0, BUF, 4) == 0 and sync() == (6, 4)
    assert do(CLOSE, 0) == 0 and sync()[0] == 3
    assert (card / SLOTS[2]).read_bytes() == b'\xAA' * 4 + payload[4:] + payload[:0x10]
    # Read: the count, then 0 at the end; the bytes reach memory
    m.put(BUF, b'\0' * 0x700)
    assert do(OPEN, 0, 0, name(SLOTS[2]), 1) == 0 and sync() == (2, 0)
    assert do(READ, 0, BUF, 0x600) == 0 and sync() == (5, 0x600)
    assert do(READ, 0, BUF + 0x600, 0x100) == 0 and sync() == (5, 0x50)
    assert do(READ, 0, BUF, 0x10) == 0 and sync() == (5, 0)
    assert bytes(m.ram[BUF:BUF + 0x650]) == (card / SLOTS[2]).read_bytes()
    assert do(CLOSE, 0) == 0 and sync()[0] == 3
    # GetDir: ".", "..", then the host's entries in byte order; host dot-files are not the card's; max
    (card / DIR / '.DS_Store').write_bytes(b'host')
    (card / DIR / 'B').write_bytes(b'')
    assert do(GETDIR, 0, 0, name(DIR + '/*'), 0, 0x12, TABLE) == 0
    count = sync()[1]
    got = [bytes(m.ram[TABLE + 0x40 * i + 0x20:TABLE + 0x40 * i + 0x40]).rstrip(b'\0') for i in range(count)]
    assert got == [b'.', b'..', b'B', b'EX_DATA.02', b'EX_DATA.03'], got
    assert do(GETDIR, 0, 0, name(DIR + '/*'), 0, 2, TABLE) == 0 and sync() == (0xD, 2)
    assert do(GETDIR, 0, 0, name(DIR + '/EX_DATA.0?'), 0, 0x12, TABLE) == 0 and sync() == (0xD, 2)
    assert do(GETDIR, 0, 0, name(DIR + '/*.03'), 0, 0x12, TABLE) == 0 and sync() == (0xD, 1)
    assert m.ram[TABLE + 0x20:TABLE + 0x2B] == b'EX_DATA.03\0'
    # port 1 is slot2
    assert do(MKDIR, 1, 0, name(DIR)) == 0 and sync() == (0xB, 0) and (root / 'slot2' / DIR).is_dir()
    # GetInfo / Sync through em_memcard_call: type 2, result 0 (a card the port knows)
    assert do(GETINFO, 0, 0, TYPE, 0, 0) == 0 and m.word(TYPE) == 2 and sync() == (1, 0)
    # the card check's GetInfo: the format word 1 (a formatted card), type 2
    m.put(TYPE, b'\xEE' * 4)
    m.put(FORMAT_WORD, b'\xEE' * 4)
    assert do(GETINFO, 0, 0, TYPE, 0, FORMAT_WORD) == 0 and sync() == (1, 0)
    assert m.word(TYPE) == 2 and m.word(FORMAT_WORD) == 1
    # refusals (each on a fresh card; the fault latches)
    mk = lambda r: (r / 'slot1' / DIR).mkdir()      # noqa: E731
    for text in ('../x', '/' + DIR, DIR + '/../x', DIR + '//x', 'a b', 'x' * 32, DIR + '/./x', ''):
        refused(OPEN, (0, 0, name(text), 0x203), 'not a relative path', mk)
    refused(GETDIR, (0, 0, name('*'), 0, 1, TABLE), 'wildcard at the card')
    refused(GETDIR, (0, 0, name(DIR + '/*'), 0, 1, TABLE), 'missing card directory')
    refused(GETDIR, (0, 0, name(DIR), 1, 1, TABLE), 'continues a listing')
    refused(OPEN, (0, 0, name(DIR + '/x'), 0x603), 'Open mode', mk)
    refused(OPEN, (0, 0, name(DIR + '/x'), 0x200), 'Open mode', mk)
    refused(OPEN, (0, 0, name(DIR + '/x'), 1), 'without create', mk)
    refused(OPEN, (0, 0, name(DIR + '/x'), 0x203), 'missing card directory')
    refused(OPEN, (0, 0, name(DIR), 0x203), 'directory', mk)
    refused(MKDIR, (0, 0, name(DIR)), 'exists', mk)
    refused(MKDIR, (0, 0, name(DIR + '/a/b')), 'missing card directory')
    refused(OPEN, (2, 0, name(DIR), 1), 'port other than')
    refused(OPEN, (0, 1, name(DIR), 1), 'port other than')
    refused(CLOSE, (0,), 'not open')
    refused(WRITE, (5, BUF, 4), 'not open')
    refused(WRITE, (0, BUF, -1), 'size')

    def three(r):
        mk(r)
        for i in range(3):
            assert do(OPEN, 0, 0, name(DIR + '/f%d' % i), 0x203) == 0 and sync() == (2, i)
    refused(OPEN, (0, 0, name(DIR + '/f3'), 0x203), 'fourth', three)

    def read_only(r):
        mk(r)
        (r / 'slot1' / DIR / 'f').write_bytes(b'abc')
        assert do(OPEN, 0, 0, name(DIR + '/f'), 1) == 0 and sync() == (2, 0)
    refused(WRITE, (0, BUF, 3), 'read-only', read_only)

    def write_only(r):
        mk(r)
        assert do(OPEN, 0, 0, name(DIR + '/f'), 0x202) == 0 and sync() == (2, 0)
    refused(READ, (0, BUF, 3), 'write-only', write_only)
    refused(WRITE, (0, 0xFFF0, 0x100), 'outside the caller', write_only)
    refused(OPEN, (0, 0, 0xFFFFFF00, 0x203), 'outside the caller')
    refused(0x114510, (0, 0, 0), 'not a libmc entry')
    refused(GETINFO, (0, 0, TYPE, FREE_WORD, FORMAT_WORD), 'free count')
    # A read-only card (EM_START_AT's start-point card, em_memcard_set_read_only):
    # the boot check makes nothing, reads answer, every other Open mode and Mkdir
    # fault, and the card's files are unchanged.
    ro = Path(tmp) / 'read_only'
    (ro / 'slot1' / DIR).mkdir(parents=True)
    (ro / 'slot1' / DIR / 'f').write_bytes(b'abc')
    LIB.em_memcard_set_read_only(1)
    try:
        LIB.em_memcard_reset()
        LIB.em_memcard_set_root(str(ro).encode())
        assert LIB.em_memcard_boot_check() == 0 and not (ro / 'slot2').exists(), 'the read-only boot check made slot2'
        (ro / 'slot2').mkdir()
        assert LIB.em_memcard_boot_check() == 1
        m.put(BUF, b'\0' * 4)
        assert do(OPEN, 0, 0, name(DIR + '/f'), 1) == 0 and sync() == (2, 0)
        assert do(READ, 0, BUF, 3) == 0 and sync() == (5, 3) and bytes(m.ram[BUF:BUF + 3]) == b'abc'
        assert do(CLOSE, 0) == 0 and sync() == (3, 0)
        for target, args in ((OPEN, (0, 0, name(DIR + '/f'), 2)), (OPEN, (0, 0, name(DIR + '/f'), 3)),
                             (OPEN, (0, 0, name(DIR + '/f'), 0x201)), (OPEN, (0, 0, name(DIR + '/g'), 0x203)),
                             (MKDIR, (0, 0, name('NEWDIR')))):
            LIB.em_memcard_reset()
            assert LIB.em_memcard_boot_check() == 1
            rc, _, fault = call(target, args, rd, wr)
            n[0] += 1
            assert rc == -1 and fault and 'read-only card' in fault, (hex(target), args, fault)
        files = sorted(str(q.relative_to(ro)) for q in ro.rglob('*'))
        assert files == ['slot1', 'slot1/' + DIR, 'slot1/' + DIR + '/f', 'slot2'], files
        assert (ro / 'slot1' / DIR / 'f').read_bytes() == b'abc'
    finally:
        LIB.em_memcard_set_read_only(0)
    LIB.em_memcard_reset()
    return n[0]


def main():
    global LIB
    t0 = time.time()
    LIB = build_lib()
    S.setup()
    S.Card = HostCard                  # both sides of the harness answer from em_memcard
    saves = [('a4', 2, 0, False), ('a4', 0, 1, True)] if not RM.FULL else \
        [(b, c, p, e) for b in ('a4', 'o7') for c in range(5) for p in (0, 1) for e in (False, True)]
    trips = [('a4', 1, 3, 0), ('o7', 4, 0, 1)] if not RM.FULL else \
        [(b, f, s, p) for b in ('a4', 'o7') for p in (0, 1) for f, s in ((1, 3), (4, 0), (0, 2))]
    with tempfile.TemporaryDirectory(prefix='em_memcard_') as tmp:
        commands = unit(tmp)
        for i, (beat, chosen, port, exists) in enumerate(saves):
            save_both(tmp, 'save%d' % i, beat, chosen, port, exists)
        calls = 0
        for i, (beat, first, second, port) in enumerate(trips):
            calls += round_trip(tmp, 'trip%d' % i, beat, first, second, port)
    LIB.em_memcard_reset()
    RM.banner(f'{commands} direct em_memcard calls', RM.part(len(saves), 40, 'whole saves on both sides'),
              RM.part(len(trips), 12, 'save + save + load round trips through the original code') +
              f' ({calls} libmc calls answered)')
    print('elapsed %.1f s' % (time.time() - t0))
    print('memcard host: PASS')


if __name__ == '__main__':
    main()
