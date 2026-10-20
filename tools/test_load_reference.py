#!/usr/bin/env python3
"""Execute the ORIGINAL memory-card load side and compare the native em_save_*
load translations byte for byte; decode what the load reads back.

docs/SAVE_GAME.md section 4. The method and the recordings are
tools/test_save_reference.py's (the user's pinned ELF and the fork's
c10/options/opt_07_load_cancel and s87/route_a01/a01_s4_east_room images;
nothing is embedded): the oracle runs the original routine over a captured
image with every callee outside the lane run as ORIGINAL code too (00121A28,
strcmp 00123020, sprintf 00122D10), nested, its entry logged; the native
module runs over a second copy whose callees run the same original code. At
every callee entry RAM, the scratchpad and the arguments must equal the
oracle's; at the end the results, the call log, RAM and scratchpad.

The lane: 002283B0 (the card screen's load enumeration), 00228EB0 (the slot
enumeration), 00228B60 (LOAD GAME's read of the chosen slot into the live
progress block), 00228C90 (the name check), 00228C50 (the check byte test),
001FE7C0 (one file read), 001FEB00 (the Read command), with the card helpers
001FE9A0 / 001FECB0 / 001FE8D0 and the leaf 00225CF0 as their options
translations.

Scripted boundary (both sides, entry checked and logged, not run): the libmc
SDK, as test_save_reference's card model, extended for the load: Open with
mode 1 opens a file the case put on the card (a missing one is refused with
-4), Read stores the file's next bytes at the buffer and returns their count,
GetDir stores its entries in the table (0x40 bytes each, the name at +0x20,
the first 0x20 bytes zero; a pattern `D/*` lists ".", "..", then D's files
in byte order), GetInfo stores the card type 2 through its first pointer.
These answers are the test's inputs (the IOP's card server is not EE code),
not claims about the server; em_memcard's own answers are test_memcard_host's.

Cases: the Read command and the file reader on every phase and result; the
check byte test and the name check over constructed tables (every name
missing in turn, a short count, near-miss names); each enumeration state;
whole load enumerations frame by frame from 002283B0's first call (no
directory, every mix of saved and New Game slots, both ports, busy Syncs, a
refused or failing command at each of the 18 commands, short reads, a
changed slot byte, a missing file), each checked against the slot records
the files imply; LOAD GAME's read of every slot into D_00810700 (and its
failures), checked against the file's bytes.

EM_TEST_FULL=1 runs every case; the default a covering sample. Commands:
`debug <group>[:i,..]`, `cover`.
"""
import multiprocessing
import os
import struct
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM  # noqa: E402
import test_save_reference as S  # noqa: E402
import test_area01_render_reference as R  # noqa: E402
import test_area01_ui_reference as U  # noqa: E402
from test_player_slide_reference import RETURN, STACK_TOP, sx32  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
NEG = 0xFFFFFFFF
MC, BLOCK = S.MC, S.BLOCK
OPEN, CLOSE, GETDIR, SYNC = S.OPEN, S.CLOSE, S.GETDIR, S.SYNC
READ, GETINFO = 0x114630, 0x114988
C58, C5C, C64 = S.C58, S.C5C, S.C64
RFD, RCOUNT = 0x275844, 0x275C6C            # the read side's descriptor and count
TABLE, STAGE, PATH = 0x821500, 0x822050, 0x822010
DIR = 'BASCUS-97112-DS00-00'
FILES = [DIR, 'EX.ico', 'icon.sys'] + ['EX_DATA.%02d' % n for n in range(5)]

LOAD_LANE = {
    0x2283B0: 0x138, 0x228EB0: 0x2B8, 0x228B60: 0xE8, 0x228C90: 0x220, 0x228C50: 0x38, 0x1FE7C0: 0x104,
    0x1FEB00: 0x88, 0x1FECB0: 0x84, 0x1FE8D0: 0x40, 0x225CF0: 0x30,
    # the save lane's command routines and the Sync poll the load reaches
    0x1FE9E0: 0x88, 0x1FEA70: 0x88, 0x1FEC20: 0x88, 0x1FE9A0: 0x38,
}
LOAD_ENTRIES = {
    0x2283B0: ([S.U32], True), 0x228EB0: ([S.U32], True), 0x228B60: ([S.U32], True),
    0x228C90: ([S.U64], True), 0x228C50: ([S.U32], True), 0x1FE7C0: ([S.U64] * 6, True),
    0x1FEB00: ([S.U64] * 3, True),
}
# Every callee the lane reaches outside it; the libmc SDK is the scripted boundary.
LOAD_WSPEC = {
    OPEN: (4, 0, 'v0'), CLOSE: (1, 0, 'v0'), READ: (3, 0, 'v0'), GETDIR: (6, 0, 'v0'), GETINFO: (5, 0, 'v0'),
    SYNC: (3, 0, 'v0'), 0x121A28: (3, 0, None), 0x123020: (2, 0, 'v0'), 0x122D10: (4, 0, None),
}


BASE_EE = U.UIEE


class LoadEE(BASE_EE):
    """The UI lane's interpreter with PSUBW, which strcmp 00123020 (00228C90's
    name check) runs: four 32-bit lanes, each a - b mod 2^32 (as
    test_memcard_host's)."""

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
        return BASE_EE.mmi(self, word, pc)


def install_lane():
    """The save reference's harness, re-aimed at the load lane."""
    S.LANE = dict(LOAD_LANE)
    S.TRPC = frozenset(pc for a, n in S.LANE.items() for pc in range(a, a + n, 4))
    S.ENTRIES = dict(LOAD_ENTRIES)
    S.WSPEC = dict(LOAD_WSPEC)
    S.STUBS = {OPEN, CLOSE, READ, GETDIR, GETINFO, SYNC}
    S.SDK = {**S.SDK, READ: 5, GETINFO: 1}
    S.Card = LoadCard
    S.UNREACHED = set()
    U.UIEE = LoadEE                         # strcmp 00123020 runs PSUBW


def entry(name):
    raw = name.encode('latin-1')
    return bytes(0x20) + raw + bytes(0x20 - len(raw))


class LoadCard(S.Card):
    """S.Card plus the load's commands. CARD keys (beyond S.Card's): 'files'
    {path: bytes} on the card at the start, 'info' (GetInfo's result),
    'shortread' {k: count} (the k-th command, a Read, stores and reports
    `count` bytes)."""

    def __init__(self, cfg):
        super().__init__(cfg)
        self.files = dict(cfg.get('files', {}))
        self.dirs |= {n.split('/')[0] for n in self.files}
        self.pos = {}
        self.store = None

    def answer(self, mem, save, address, args):
        self.store = save
        return super().answer(mem, save, address, args)

    def put_bytes(self, at, data):
        for i, b in enumerate(data):
            self.store((at & MASK) + i, b, 1)

    def serve(self, mem, address, args, k):
        if address == OPEN:
            name, mode = S.cstr(mem, args[2]), args[3] & MASK
            self.order.append(('open', name, mode))
            if not mode & 0x200 and name not in self.files:
                return -4
            fd, self.next_fd = self.next_fd, self.next_fd + 1
            self.fds[fd], self.pos[fd] = name, 0
            return fd
        if address == READ:
            fd, buf, size = args[0] & MASK, args[1] & MASK, args[2] & MASK
            name = self.fds.get(fd)
            if name is None:
                self.order.append(('read', '?', 0))
                return -5
            data = self.files.get(name, b'')[self.pos[fd]:self.pos[fd] + size]
            count = min(len(data), self.cfg.get('shortread', {}).get(k, len(data)))
            self.put_bytes(buf, data[:count])
            self.pos[fd] += count
            self.order.append(('read', name, count))
            return count
        if address == CLOSE:
            fd = args[0] & MASK
            self.pos.pop(fd, None)
            if fd not in self.fds:
                self.order.append(('close', '?'))
                return -5
            return super().serve(mem, address, args, k)
        if address == GETDIR:
            name, most, table = S.cstr(mem, args[2]), args[4] & MASK, args[5] & MASK
            self.order.append(('getdir', name))
            if name.endswith('/*'):
                d = name[:-2]
                names = ['.', '..'] + sorted(n.split('/', 1)[1] for n in self.files if n.startswith(d + '/'))
                if d not in self.dirs:
                    names = []
            else:
                names = [name] if name in self.dirs else []
            names = names[:most]
            for i, n in enumerate(names):
                self.put_bytes(table + 0x40 * i, entry(n))
            return len(names)
        if address == GETINFO:
            if args[2] & MASK:
                self.put_bytes(args[2], struct.pack('<I', 2))
            self.order.append(('getinfo',))
            return self.cfg.get('info', 0)
        return super().serve(mem, address, args, k)


# ---- the card contents -------------------------------------------------------

def live_block(beat):
    return bytes(R.IMAGES[beat][0][BLOCK:BLOCK + 0x640])


def saved(block, tag=0):
    """A slot as the writer leaves it: +0xF = 1 and the check byte (the
    distinct header bytes make every record store visible)."""
    b = bytearray(block)
    for i in range(6):
        b[i] = (0x21 + 0x10 * tag + i) & 0xFF
    b[0xA] = 0x40 + tag
    b[0x50:0x54] = struct.pack('<I', 0x1000 * (tag + 1) + 0x37)
    b[0xF] = 1
    b[0x63F] = (0x100 - sum(b[:0x63F])) & 0xFF
    return bytes(b)


def blank(block):
    """A New Game slot: +0xF = 0, no check byte."""
    b = bytearray(block)
    b[0xF] = 0
    return bytes(b)


def card_files(beat, present, drop=(), flip=None):
    block = live_block(beat)
    out = {DIR + '/' + DIR: bytes(0x48), DIR + '/EX.ico': bytes(range(256)) * 4, DIR + '/icon.sys': bytes(0x3C4)}
    for n in range(5):
        out[DIR + '/EX_DATA.%02d' % n] = saved(block, n) if n in present else blank(block)
    if flip is not None:
        name, at = flip
        data = bytearray(out[name])
        data[at] ^= 1
        out[name] = bytes(data)
    for d in drop:
        out.pop(DIR + '/' + d)
    return out


def expected_load(files):
    """What 002283B0 must leave, from the files alone: (result, present
    slots, records), or (3, ..) when the original's checks refuse the card."""
    if not any(n.startswith(DIR + '/') for n in files):
        return 1, set(), {}
    if any(DIR + '/' + f not in files for f in FILES):
        return 3, None, None
    present, records = set(), {}
    for n in range(5):
        blk = files[DIR + '/EX_DATA.%02d' % n]
        if len(blk) != 0x640:
            return 3, None, None
        if blk[0xF] == 1:
            if sum(blk) & 0xFF:
                return 3, None, None
            present.add(n)
            records[n] = blk[0:6] + blk[0xA:0xB] + blk[0x50:0x54]
    return 1, present, records


# ---- case helpers --------------------------------------------------------------

def rec(port=0, chosen=0, load=0, state=0, cursor=0, st=0, count=0):
    return [(MC + 2, load, 1), (MC + 5, state, 1), (MC + 6, cursor, 1), (MC + 0xA, chosen, 1),
            (MC + 0x48, port, 4), (MC + 0x4C, 0, 4), (MC + 0x50, 0, 4), (MC + 0x54, 0, 4), (MC + 0x58, 0, 4),
            (MC + 0x64, st, 4), (MC + 0x68, count, 4)]


def helpers(err=0, phase=0, last=0, count=0, fd=NEG):
    return [(C58, err, 4), (C5C, phase, 4), (C64, last, 4), (RCOUNT, count, 4), (RFD, fd, 4),
            (0x275C68, 0, 4), (0x275840, NEG, 4)]


def table_bytes(names):
    return [(TABLE + 0x40 * i + j, b, 1) for i, n in enumerate(names) for j, b in enumerate(entry(n))]


def stage_bytes(block):
    return [(STAGE + i, b, 1) for i, b in enumerate(block)]


def prerun(spad, fn, frames):
    """The oracle alone: the frame of the first nonzero result and it."""
    U.WSPEC = {a: spec for a, spec in S.WSPEC.items() if a not in S.LANE}
    ee = U.oracle_ee(spad)
    S.oracle_hooks(ee)
    for f in range(frames):
        ee.r[4], ee.r[29], ee.r[31] = MC, STACK_TOP, RETURN
        ee.run(fn)
        v = ee.r[2] & MASK
        if v:
            return f + 1, v
    return frames, 0


def one_case(item):
    group, beat, knobs = item
    S.CARD = knobs.get('card', {})
    ram, spad = S.make(beat, knobs)
    steps = [('call', knobs['fn'], tuple(knobs.get('args', ())))] * knobs.get('frames', 1)
    return S.run_both(item, bytes(spad), steps)


# ---- items ------------------------------------------------------------------------

SLOT0 = 0x267060


def rcmd_items():
    out = []
    args = (3, STAGE, 0x640)
    for beat in ('o7', 'a4'):
        for v0 in (0, 1, NEG):
            out.append(('rcmd', beat, {'fn': 0x1FEB00, 'args': args, 'b': helpers(phase=0),
                                       'card': {'issue': {0: v0}} if v0 else {}}))
        for last in (0, 3, 0x640, NEG, 0xFFFFFFFC):
            out.append(('rcmd', beat, {'fn': 0x1FEB00, 'args': args, 'b': helpers(phase=5, last=last)}))
        for other in (6, 7, NEG):
            out.append(('rcmd', beat, {'fn': 0x1FEB00, 'args': args, 'b': helpers(phase=other)}))
    return out


def rfile_items():
    out = []
    args = (MC + 0x64, 0, 0, SLOT0, STAGE, 0x640)
    files = {DIR + '/EX_DATA.00': bytes(0x640)}
    for beat in ('o7', 'a4'):
        for st in (0, 1, 2, 3, NEG):
            for err in (0, 1):
                for phase, last, count in ((0, 0, 0), (2, 5, 0), (2, NEG, 0), (5, 0x640, 0), (5, 0x10, 0),
                                           (5, NEG, 0), (3, 0, 0x640), (3, 0, 0x63F), (3, 1, 0x640), (9, 0, 0)):
                    for v0 in ((0, 1) if phase == 0 else (0,)):
                        card = {'files': files, **({'issue': {0: v0}} if v0 else {})}
                        out.append(('rfile', beat, {'fn': 0x1FE7C0, 'args': args, 'card': card,
                                                    'b': [(MC + 0x64, st, 4)] + helpers(err, phase, last, count, 4)}))
    return out


def check_items():
    out = []
    full = ['.', '..'] + sorted(FILES)
    for beat in ('o7', 'a4'):
        block = live_block(beat)
        for data in (saved(block), blank(block), saved(block, 3), bytes(0x640), bytes([0xFF]) * 0x640):
            out.append(('check', beat, {'fn': 0x228C50, 'args': (STAGE,), 'b': stage_bytes(data)}))
        out.append(('check', beat, {'fn': 0x228C50, 'args': (BLOCK,)}))
        out.append(('check', beat, {'fn': 0x228C90, 'args': (len(full),), 'b': table_bytes(full)}))
        out.append(('check', beat, {'fn': 0x228C90, 'args': (len(full),), 'b': table_bytes(full[::-1])}))
        for gone in FILES:
            names = [n for n in full if n != gone]
            out.append(('check', beat, {'fn': 0x228C90, 'args': (len(names),), 'b': table_bytes(names)}))
        for n in (0, 1, len(full) - 1, 0x12):
            out.append(('check', beat, {'fn': 0x228C90, 'args': (n,), 'b': table_bytes(full)}))
        out.append(('check', beat, {'fn': 0x228C90, 'args': (MASK64,), 'b': table_bytes(full)}))
        near = ['.', '..', DIR + 'X', 'EX.ic', 'icon.sys.', 'EX_DATA.0', 'EX_DATA.000', 'ex_data.01', 'EX_DATA.05']
        out.append(('check', beat, {'fn': 0x228C90, 'args': (len(near),), 'b': table_bytes(near)}))
    return out


def enum_items():
    out = []
    full = ['.', '..'] + sorted(FILES)
    for beat in ('o7', 'a4'):
        files = card_files(beat, {0, 2})
        block = live_block(beat)
        # 00228EB0 state 0
        out.append(('enum', beat, {'fn': 0x228EB0, 'args': (MC,), 'b': rec(state=0, cursor=3, count=2)}))
        # state 1: the listing issued, refused, or its result over a table in RAM
        for v0 in (0, 1):
            out.append(('enum', beat, {'fn': 0x228EB0, 'args': (MC,), 'b': rec(1, state=1) + helpers(),
                                       'card': {'files': files, **({'issue': {0: v0}} if v0 else {})}}))
        out.append(('enum', beat, {'fn': 0x228EB0, 'args': (MC,), 'b': rec(state=1) + helpers(0, 7)}))
        for last, names in ((10, full), (9, full[:-1]), (0, []), (NEG, full), (10, full[::-1])):
            out.append(('enum', beat, {'fn': 0x228EB0, 'args': (MC,),
                                       'b': rec(state=1) + helpers(0, 0xD, last) + table_bytes(names)}))
        # state 2: each read outcome, the staging block's kinds, the cursor's range
        for cursor in (0, 2, 4):
            for stage in (saved(block, cursor), blank(block), saved(block, cursor)[:0x100] + b'\x01' +
                          saved(block, cursor)[0x101:]):
                for count in (0x640, 0x63F):
                    out.append(('enum', beat, {'fn': 0x228EB0, 'args': (MC,),
                                               'b': rec(0, state=2, cursor=cursor, st=2, count=1) +
                                               helpers(0, 3, 0, count, 4) + stage_bytes(stage)}))
            out.append(('enum', beat, {'fn': 0x228EB0, 'args': (MC,), 'b': rec(1, state=2, cursor=cursor) + helpers(),
                                       'card': {'files': files}}))
            out.append(('enum', beat, {'fn': 0x228EB0, 'args': (MC,),
                                       'b': rec(0, state=2, cursor=cursor, st=1) + helpers(1, 9, 0, 0, 4)}))
        for cursor in (5, 0xFF):
            out.append(('enum', beat, {'fn': 0x228EB0, 'args': (MC,), 'b': rec(state=2, cursor=cursor)}))
        # state 3: GetInfo issued, refused, its results, a foreign phase
        for v0 in (0, 1):
            out.append(('enum', beat, {'fn': 0x228EB0, 'args': (MC,), 'b': rec(1, state=3) + helpers(),
                                       'card': {'issue': {0: v0}} if v0 else {}}))
        for last in (0, 1, 5, NEG):
            out.append(('enum', beat, {'fn': 0x228EB0, 'args': (MC,), 'b': rec(state=3) + helpers(0, 1, last)}))
        out.append(('enum', beat, {'fn': 0x228EB0, 'args': (MC,), 'b': rec(state=3) + helpers(0, 7)}))
        for state in (4, 0xFF):
            out.append(('enum', beat, {'fn': 0x228EB0, 'args': (MC,), 'b': rec(state=state)}))
        # 002283B0: the Sync gate, each state, 00228EB0's four results
        for pend, busy in ((0, 0), (0xD, 0), (0xD, 1)):
            for load, b, card in (
                    (0, helpers(), {'files': files}), (0, helpers(), {}), (0, helpers(0, 0xD, 0), {}),
                    (0, helpers(0, 0xD, 1), {}), (0, helpers(0, 0xD, NEG), {}), (0, helpers(1, 0xD, NEG), {}),
                    (1, rec(state=0)[1:2] + helpers(), {}),
                    (1, rec(state=3)[1:2] + helpers(0, 1, 0), {}),
                    (1, rec(state=3)[1:2] + helpers(0, 1, 5), {}),
                    (1, rec(state=1)[1:2] + helpers(0, 0xD, 3) + table_bytes(full[:3]), {}),
                    (2, helpers(), {}), (3, helpers(1), {}), (3, helpers(), {})):
                if busy and load:
                    continue
                out.append(('enum', beat, {'fn': 0x2283B0, 'args': (MC,), 'b': rec(load=load) + b,
                                           'card': {**card, 'pending': pend, 'busy': busy}}))
    return out


def load_items():
    """Whole load enumerations from 002283B0's first call."""
    out = []
    for beat in ('a4', 'o7'):
        for present in ((), (0,), (1, 3), (0, 1, 2, 3, 4), (4,)):
            for port in (0, 1):
                if port and present not in ((1, 3),):
                    continue
                out.append(('load', beat, {'port': port, 'present': present}))
        out.append(('load', beat, {'port': 0, 'present': None}))                       # no directory
        out.append(('load', beat, {'port': 0, 'present': (2,), 'card': {'busy': 2}}))
        out.append(('load', beat, {'port': 1, 'present': (0, 4), 'card': {'busy': 1}}))
        out.append(('load', beat, {'port': 0, 'present': (2,), 'flip': ('EX_DATA.02', 0x100)}))
        out.append(('load', beat, {'port': 0, 'present': (2,), 'flip': ('EX_DATA.03', 0x100)}))
        for d in ('icon.sys', 'EX.ico', DIR, 'EX_DATA.02'):
            out.append(('load', beat, {'port': 0, 'present': (1,), 'drop': (d,)}))
        out.append(('load', beat, {'port': 0, 'present': (1,), 'card': {'info': 3}}))
        out.append(('load', beat, {'port': 0, 'present': (1,), 'card': {'shortread': {6: 0x100}}}))
        # a failing result or a refused issue at each of the 18 commands
        for k in range(18):
            out.append(('load', beat, {'port': 0, 'present': (1, 2), 'card': {'result': {k: -4}}}))
            if k % 3 == 0:
                out.append(('load', beat, {'port': 0, 'present': (1, 2), 'card': {'issue': {k: 1}}}))
    return out


LOAD_FRAMES = 80


def load_case(item):
    group, beat, knobs = item
    present = knobs['present']
    files = {} if present is None else card_files(beat, set(present), knobs.get('drop', ()),
                                                  (DIR + '/' + knobs['flip'][0], knobs['flip'][1])
                                                  if knobs.get('flip') else None)
    cfg = dict(knobs.get('card', {}), files=files)
    S.CARD = cfg
    b = rec(knobs['port']) + helpers() + [(MC + 0x6C + n, 0xEE, 1) for n in range(5)]
    b += [(MC + 0x74 + i, 0xEE, 1) for i in range(60)]
    ram, spad = S.make(beat, {'b': b})
    frames, value = prerun(bytes(spad), 0x2283B0, LOAD_FRAMES * (1 + cfg.get('busy', 0)))
    failing = cfg.get('result') or cfg.get('issue') or cfg.get('shortread') or cfg.get('info')
    want, want_present, records = expected_load(files)
    if not failing:
        assert value == want, (item, 'load result', value, want)
    else:
        assert value in (2, 3), (item, 'a failing card was accepted', value)

    def check(ee):
        if failing or want != 1:
            return True
        got = [ee.load(MC + 0x6C + n, 1) for n in range(5)]
        assert got == [1 if n in want_present else 0 for n in range(5)], (item, 'slots present', got)
        if present is not None:
            assert ee.load(MC + 0x68) == len(want_present), (item, 'count')
        for n, r in records.items():
            got = bytes(ee.mem[MC + 0x74 + 12 * n:MC + 0x80 + 12 * n])
            assert got[:7] + got[8:] == r, (item, 'slot record', n, got.hex(), r.hex())
        return True
    r = S.run_both(item, bytes(spad), [('call', 0x2283B0, (MC,))] * frames, check=check)
    assert S.MODELS['oracle'].order == S.MODELS['native'].order, (item, 'command order differs')
    return r


def restore_items():
    """LOAD GAME's read 00228B60 of the chosen slot into D_00810700."""
    out = []
    for beat in ('a4', 'o7'):
        for chosen in range(5):
            for port in (0, 1):
                if port and chosen not in (0, 4):
                    continue
                out.append(('restore', beat, {'chosen': chosen, 'port': port}))
        out.append(('restore', beat, {'chosen': 2, 'port': 0, 'card': {'busy': 2}}))
        out.append(('restore', beat, {'chosen': 3, 'port': 0, 'drop': ('EX_DATA.03',)}))
        out.append(('restore', beat, {'chosen': 1, 'port': 0, 'card': {'shortread': {1: 0x63F}}}))
        for k in range(3):
            out.append(('restore', beat, {'chosen': 1, 'port': 1, 'card': {'result': {k: -4}}}))
            out.append(('restore', beat, {'chosen': 1, 'port': 1, 'card': {'issue': {k: 1}}}))
        out.append(('restore', beat, {'chosen': 1, 'port': 0, 'err': 1}))
    return out


def restore_case(item):
    group, beat, knobs = item
    files = card_files(beat, {0, 1, 2, 3, 4}, knobs.get('drop', ()))
    cfg = dict(knobs.get('card', {}), files=files)
    S.CARD = cfg
    b = rec(knobs['port'], knobs['chosen']) + helpers(knobs.get('err', 0))
    ram, spad = S.make(beat, {'b': b})
    frames, value = prerun(bytes(spad), 0x228B60, 12 * (1 + cfg.get('busy', 0)))
    failing = cfg.get('result') or cfg.get('issue') or cfg.get('shortread') or knobs.get('drop') or knobs.get('err')
    name = DIR + '/EX_DATA.%02d' % knobs['chosen']
    if failing:
        assert value == 2, (item, 'a failing read reported', value)
    else:
        assert value == 1, (item, 'the read did not complete', value, frames)

    def check(ee):
        if not failing:
            assert bytes(ee.mem[BLOCK:BLOCK + 0x640]) == files[name], (item, 'D_00810700 is not the slot file')
            assert S.cstr(ee.mem, PATH) == name, (item, 'the path', S.cstr(ee.mem, PATH))
        return True
    r = S.run_both(item, bytes(spad), [('call', 0x228B60, (MC,))] * frames, check=check)
    assert S.MODELS['oracle'].order == S.MODELS['native'].order, (item, 'command order differs')
    return r


def case_groups():
    return [('rcmd', rcmd_items(), one_case), ('rfile', rfile_items(), one_case), ('check', check_items(), one_case),
            ('enum', enum_items(), one_case), ('load', load_items(), load_case),
            ('restore', restore_items(), restore_case)]


# Branch outcomes no input reaches. 001FE7C0 returns only -1, 0 or 1, so the
# callers' arm after those three tests never sees another value (00228B60's
# zero test, 00228EB0's -1 test); 00228EB0 returns only 0..3, so 002283B0's
# zero test after its 3 / 2 / 1 tests always holds; 00225CF0 is only called
# with the bounds 5..7, so its empty-range test never holds.
UNREACHED = {(0x228C00, False), (0x228FE0, False), (0x228490, False), (0x225CF4, True)}

# The default run: the greedy branch-outcome cover of the full sweep (the
# `cover` command prints it; item indices into each group's deterministic
# list), plus the decodes: every check case (each name missing in turn),
# the loads of one, two and five saved slots and of no directory, a changed
# slot byte (load 0..6, 9), and a read error's reset 001FE8D0 (enum 16).
QUICK = {
    'enum': [2, 3, 7, 10, 13, 16, 36, 41, 42, 46, 52, 55, 66],
    'load': [0, 1, 2, 4, 6, 7, 9],
    'rcmd': [1, 6, 8],
    'restore': [0, 7, 8, 9],
    'rfile': [1, 4, 45, 46, 53, 66],
}


def setup():
    install_lane()
    S.setup()


def sample(items, name):
    if RM.FULL or name not in QUICK:
        return items
    return [items[i] for i in QUICK[name]]


def run_groups(full):
    per, outcomes, cases, calls, entries, counts = [], set(), 0, 0, 0, []
    for name, items, fn in case_groups():
        chosen = items if full else sample(items, name)
        for i, r in enumerate(S.pmap(fn, chosen)):
            cases += 1
            if r in ('unmeasured', 'skipped'):
                continue
            outcomes.update(r[0])
            calls += r[1]
            entries += r[2]
            per.append((name, i, frozenset(o for o in r[0] if o[0] in S.lane_pcs())))
        counts.append(RM.part(len(chosen), len(items), name + ' cases'))
    return per, outcomes, cases, calls, entries, counts


def with_pool(fn):
    if S.JOBS > 1 and 'fork' in multiprocessing.get_all_start_methods():
        S.POOL = multiprocessing.get_context('fork').Pool(S.JOBS)
    try:
        return fn()
    finally:
        if S.POOL is not None:
            R.close_pool(S.POOL)
            S.POOL = None


def cover():
    setup()
    per = with_pool(lambda: run_groups(True))[0]
    left = set().union(*(o for _, _, o in per))
    chosen = {}
    while left:
        name, i, o = max(per, key=lambda t: len(t[2] & left))
        chosen.setdefault(name, []).append(i)
        left -= o
    for must, idx in (('load', {0, 1, 2, 4, 6, 9}), ('restore', {0})):   # the decodes, every run
        chosen[must] = sorted(set(chosen.get(must, [])) | idx)
    print('QUICK = {')
    for name in sorted(chosen):
        print('    %r: %r,' % (name, sorted(chosen[name])))
    print('}')


def main():
    t0 = time.time()
    setup()
    _, outcomes, cases, calls, entries, counts = with_pool(lambda: run_groups(RM.FULL))
    branches = S.all_branches()
    need = {(pc, t) for pc in branches for t in (True, False)}
    got = {o for o in outcomes if o[0] in S.lane_pcs()}
    missing = sorted((hex(pc), t) for pc, t in need - got if (pc, t) not in UNREACHED)
    RM.banner(*counts, f'{cases} cases', f'{calls} callee calls, {entries} entries with RAM + scratchpad + '
              'arguments equal to the original\'s',
              f'{len(branches)} conditional branches: {len(need & got)} of {len(need)} outcomes seen')
    if RM.FULL:
        assert not missing, ('branch outcomes never seen', missing)
    elif missing:
        print('outcomes not in this sample (EM_TEST_FULL=1 requires all):', len(missing))
    if missing and os.environ.get('EM_SAVE_SHOW_MISSING'):
        print('missing:', missing)
    print('elapsed %.1f s' % (time.time() - t0))
    print('load reference: PASS')


def debug(names):
    setup()
    table = {name: (items, fn) for name, items, fn in case_groups()}
    for name in names:
        group, _, idx = name.partition(':')
        items, fn = table[group]
        chosen = items if not idx else [items[int(i)] for i in idx.split(',')]
        for it in chosen:
            t = time.time()
            try:
                r = fn(it)
                print('ok', repr(it)[:160], 'calls', r[1] if r not in ('unmeasured', 'skipped') else r,
                      '%.2fs' % (time.time() - t))
            except AssertionError as ex:
                print('FAIL', repr(it)[:200], repr(ex)[:3000])


if __name__ == '__main__':
    _ = sx32
    if len(sys.argv) > 1 and sys.argv[1] == 'debug':
        debug(sys.argv[2:])
    elif len(sys.argv) > 1 and sys.argv[1] == 'cover':
        cover()
    else:
        main()
