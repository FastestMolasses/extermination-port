#!/usr/bin/env python3
"""Execute the ORIGINAL memory-card screen past the slot choice and compare
the native em_card_* translations byte for byte.

docs/SAVE_GAME.md section 6. The method, the harness and the card model are
tools/test_save_reference.py's and tools/test_load_reference.py's (the
user's pinned ELF and the fork's c10/options/opt_07_load_cancel and
c10/damage/dmg_05_load_screen images, both on the card screen's load row;
nothing is embedded). The oracle runs the original routine over a captured
image with every callee outside the lane run as ORIGINAL code too (the text
leaves under 001FCBD0, 001CC1E0, 001FC770, sprintf 00122D10, the cues
0020CD40 / 60 / A0, 00121A28 ...), nested, its entry logged; the native
module runs over a second copy whose callees run the same original code. At
every callee entry RAM, the scratchpad and the arguments must equal the
oracle's; at the end the results, the call log, RAM and scratchpad.

The lane: the card screen 00227300 and the routines below it that were not
translated before (002280F0, 00228320, 002293B0, 00229170, 00227980,
00227010, 00227160, 00226E90, 00227C60, 00227DE0, 00228070, 001FED40,
00227270, 00226B90, 002267A0, 00226D50), with the save and load lanes'
translations (em_save_*) and the options lane's text and card helpers
(001FCBD0, 001FE9A0, 001FECB0, 001FE8D0, 00225CF0, 00225A20, 00226010,
002256E0, 00225700) as theirs.

Scripted boundary (both sides, entry checked and logged, not run): the
libmc SDK, test_load_reference's card model plus GetInfo's free and format
words (stored through a3 / t0 when they are nonzero) and Format 00114D18
(the card emptied, result 0). The answers are the test's inputs, not claims
about the IOP's card server (em_memcard's own answers are
test_memcard_host's). sprintf 00122D10 is logged with the argument
registers its format consumes.

Cases: each routine on its states, with the pad words D_00810E74 / E78, the
GetInfo results and types, the Sync gate and the error flag; whole card
screens frame by frame from 00227300's first call, with a pad script made
on the oracle alone from the screen's state (a press every other frame):
a load of each slot (D_00810700 checked against the slot file), a save to a
new card and over an existing directory (the card checked against
SAVE_GAME.md section 2), a declined confirmation, Circle / Triangle out of
each screen, a card without a directory, an unformatted card (the format
prompt, Yes and No), a card too full, a removed card, a rejected card, a
failing read and write.

EM_TEST_FULL=1 runs every case; the default a covering sample. Commands:
`debug <group>[:i,..]`, `cover`.
"""
import ctypes as C
import multiprocessing
import os
import random
import struct
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
os.environ.setdefault('EM_LANE', 'card')
import reference_mode as RM  # noqa: E402
import reference_root as refs  # noqa: E402
import test_save_reference as S  # noqa: E402
import test_load_reference as L  # noqa: E402
import test_options_reference as O  # noqa: E402
import test_area01_render_reference as R  # noqa: E402
import test_area01_ui_reference as U  # noqa: E402
from test_player_slide_reference import RETURN, STACK_TOP  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
NEG = 0xFFFFFFFF
OUT = ROOT / 'build' / 'card' / 'reference'
R.OUT = U.OUT = S.OUT = OUT

MC, BLOCK = S.MC, S.BLOCK
OPEN, MKDIR, CLOSE, WRITE, GETDIR, SYNC = S.OPEN, S.MKDIR, S.CLOSE, S.WRITE, S.GETDIR, S.SYNC
READ, GETINFO = L.READ, L.GETINFO
FORMAT = 0x114D18
C58, C5C, C64 = S.C58, S.C5C, S.C64
E74, E78 = 0x810E74, 0x810E78
CB2 = 0x810CB2
PACKET = 0x811CD4      # the text's packet pointer; each 00227300 frame advances it, the main loop resets it
DIR = L.DIR
SPRINTF = 0x122D10

CAP = refs.capture('c10')
IMAGES = {
    'o7': CAP / 'options' / 'opt_07_load_cancel',
    'd5': CAP / 'damage' / 'dmg_05_load_screen',
}

CARD_LANE = {
    0x227300: 0x678, 0x2280F0: 0x22C, 0x228320: 0x8C, 0x2293B0: 0x288, 0x229170: 0x238, 0x227980: 0x2DC,
    0x227010: 0x14C, 0x227160: 0x10C, 0x226E90: 0x17C, 0x227C60: 0x17C, 0x227DE0: 0x28C, 0x228070: 0x7C,
    0x1FED40: 0x88, 0x227270: 0x90, 0x226B90: 0x1BC, 0x2267A0: 0x3EC, 0x226D50: 0x140,
}
OPTIONS_LANE = {a: O.LANE[a] for a in (0x1FCBD0, 0x1FE9A0, 0x1FECB0, 0x1FE8D0, 0x225CF0, 0x225A20, 0x226010,
                                       0x2256E0, 0x225700, 0x1FE920)}
LANE = {**S.LANE, **L.LOAD_LANE, **OPTIONS_LANE, **CARD_LANE}

P, U8, U32, U64 = C.POINTER, C.c_uint8, C.c_uint32, C.c_uint64
ENTRIES = {a: ([U64] * 2, True) if a == 0x1FED40 else (([], True) if a == 0x227270 else
                                                       ([U32], a != 0x2293B0)) for a in CARD_LANE}
SOURCES = ['src/game/em_card_original.c', 'src/game/em_save_original.c', 'src/game/em_options_original.c',
           'src/game/em_startup_load_gaps.c']

WSPEC = {**O.WSPEC, **S.WSPEC, **L.LOAD_WSPEC, FORMAT: (2, 0, 'v0'), GETINFO: (5, 0, 'v0')}
TEXT_LEAVES = (0x1CC1E0, 0x1FC770)          # the glyph packets 001FCBD0 / 002293B0 draw
STUBS = {OPEN, MKDIR, CLOSE, WRITE, READ, GETDIR, GETINFO, SYNC, FORMAT}


def build_native():
    import hashlib
    import subprocess
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('card' + ('.dylib' if sys.platform == 'darwin' else '.so'))
    h = hashlib.sha256(repr(S.CFLAGS).encode())
    for q in list(SOURCES) + sorted(str(x) for x in (ROOT / 'src').rglob('*.h')):
        h.update(str(q).encode())
        h.update((ROOT / q).read_bytes())
    stamp = lib.with_suffix(lib.suffix + '.sha256')
    if not (lib.exists() and stamp.exists() and stamp.read_text() == h.hexdigest()):
        subprocess.run(['cc'] + S.CFLAGS + list(SOURCES) + ['-o', str(lib)], cwd=ROOT, check=True)
        stamp.write_text(h.hexdigest())
    native = C.CDLL(str(lib))
    for address, (args, result) in ENTRIES.items():
        fn = getattr(native, 'em_card_%08X' % address)
        fn.argtypes = [P(U.UiState)] + args + ([P(U32)] if result else [])
        fn.restype = C.c_int
    return native


# ---- the card model -------------------------------------------------------------

class CardModel(L.LoadCard):
    """L.LoadCard plus GetInfo's three words and Format. CARD keys (beyond
    LoadCard's): 'types' / 'formats' / 'frees' (per GetInfo call, the last
    repeating; defaults 2, 1, 8000), 'infos' (GetInfo's result per call,
    the last repeating; default 0), 'format_result'."""

    def __init__(self, cfg):
        super().__init__(cfg)
        self.ninfo = 0

    @staticmethod
    def nth(seq, n, default):
        if not seq:
            return default
        return seq[min(n, len(seq) - 1)]

    def serve(self, mem, address, args, k):
        if address == GETINFO:
            n, self.ninfo = self.ninfo, self.ninfo + 1
            cfg = self.cfg
            for at, value in ((args[2], self.nth(cfg.get('types'), n, 2)), (args[3], self.nth(cfg.get('frees'), n, 8000)),
                              (args[4], self.nth(cfg.get('formats'), n, 1))):
                if at & MASK:
                    self.put_bytes(at, struct.pack('<I', value & MASK))
            self.order.append(('getinfo', args[0] & MASK))
            return self.nth(cfg.get('infos'), n, 0)
        if address == FORMAT:
            self.order.append(('format', args[0] & MASK))
            self.files.clear()
            self.dirs.clear()
            return self.cfg.get('format_result', 0)
        return super().serve(mem, address, args, k)


def conversions(mem, at):
    fmt, n, i = S.cstr(mem, at), 0, 0
    while i < len(fmt):
        if fmt[i] == '%':
            if fmt[i + 1:i + 2] == '%':
                i += 2
                continue
            n += 1
        i += 1
    return n


# Quick mode: the text leaves (and sprintf) are memoized on the oracle side
# and their stores replayed on the native side after its entry check. The
# memo's key is the entry's stack pointer and every integer argument
# register the callee is passed (U.memo_run keys on a0 alone, which these
# multi-argument leaves cannot use; none takes a float argument), and a hit
# needs every value the recorded run read (instruction fetches included)
# unchanged.
MEMO_ON = set() if RM.FULL else set(TEXT_LEAVES) | {SPRINTF}
MEMO = {}


def memo_run(e, address, nargs):
    key = (address, e.r[29] & MASK) + tuple(e.r[4 + i] & MASK64 for i in range(nargs))
    for reads, writes, v0, f0 in MEMO.get(key, ()):
        if all(e.load(a, n) == v for (a, n), v in reads):
            for is_spad, at, data in writes:
                (e.spad if is_spad else e.mem)[at:at + len(data)] = data
                e.journal.append((is_spad, at, data))
            return v0, f0
    reads, written = {}, set()
    load0, save0, write0 = e.load, e.save, e.write

    def load(a, size=4):
        v = load0(a, size)
        a &= MASK
        if (a, size) not in reads and all(b not in written for b in range(a, a + size)):
            reads[(a, size)] = v
        return v

    def save(a, value, size=4):
        a &= MASK
        written.update(range(a, a + size))
        save0(a, value, size)

    def write(a, data):
        a &= MASK
        written.update(range(a, a + len(data)))
        write0(a, data)
    j0 = len(e.journal)
    e.load, e.save, e.write = load, save, write
    try:
        v0, f0 = U.SAR.nested_bits(e, address)
    finally:
        e.load, e.save, e.write = load0, save0, write0
    MEMO.setdefault(key, []).append((tuple(reads.items()), list(e.journal[j0:]), v0, f0))
    return v0, f0


def logged(e, address, spec):
    entry = U.entry_of(e, address, spec)
    e.entries.append((len(e.journal), entry))
    if address in MEMO_ON:
        v0, f0 = memo_run(e, address, spec[0])
    else:
        v0, f0 = U.SAR.nested_bits(e, address)
    e.calls.append(entry + (U.result_of(spec, v0, f0),))
    e.exits.append((len(e.journal), v0, f0))
    e.r[2], e.f[0] = v0, f0


def sprintf_hook(e):
    logged(e, SPRINTF, (2 + conversions(e.mem, e.r[5]), 0, None))


def oracle_hooks(ee):
    S.oracle_hooks(ee)
    ee.hooks[SPRINTF] = sprintf_hook
    for a in TEXT_LEAVES:
        ee.hooks[a] = lambda e, a=a: logged(e, a, U.WSPEC[a])


class Native(S.Native):
    def call(self, address, args):
        types, result = ENTRIES[address]
        fn = getattr(U.NATIVE, 'em_card_%08X' % address)
        out = U32(0)
        rc = fn(C.byref(self.state), *args, *([C.byref(out)] if result else []))
        return rc, (out.value if result else None)


def install():
    S.LANE = dict(LANE)
    S.TRPC = frozenset(pc for a, n in LANE.items() for pc in range(a, a + n, 4))
    S.ENTRIES = dict(ENTRIES)
    S.WSPEC = dict(WSPEC)
    S.STUBS = set(STUBS)
    S.SDK = {**S.SDK, READ: 5, GETINFO: 1, FORMAT: 0x10}
    S.Card = FailCard
    S.IMAGES = dict(IMAGES)
    S.BEATS = list(IMAGES)
    U.UIEE = L.LoadEE
    S.install()
    U.REPLAY = set(MEMO_ON)
    U.Native = Native
    U.oracle_hooks = oracle_hooks
    U.ENTRIES = dict(ENTRIES)


def setup():
    install()
    S.ELF = U.ELF = R.ELF = S.read_elf()
    S.NATIVE = U.NATIVE = build_native()
    install()
    U.ELF = R.ELF = S.ELF
    R.TRACKER = R.build_tracker()
    S.load_images()
    S.check_code()
    got = S.callee_set()
    missing = sorted(hex(a) for a in got if a not in WSPEC and a not in U.INLINE)
    assert not missing, ('callees without a spec', missing)


def run_both(item, spad, steps, check=None):
    return S.run_both(item, spad, steps, check=check)


# ---- case helpers ------------------------------------------------------------------

def rec(mode=2, port=0, screen=0, step=0, st3=0, st4=0, yes=0, chosen=0, poll=0, present=(), count=0,
        retry=0, frames=0):
    b = [(MC + 1, screen, 1), (MC + 2, step, 1), (MC + 3, st3, 1), (MC + 4, st4, 1), (MC + 5, 0, 1),
         (MC + 6, 0, 1), (MC + 7, poll, 1), (MC + 9, yes, 1), (MC + 0xA, chosen, 1), (MC + 0x10, frames, 4),
         (MC + 0x14, mode, 1), (MC + 0x15, 2, 1), (MC + 0x20, retry, 4), (MC + 0x28, 1, 4), (MC + 0x2C, 1, 4),
         (MC + 0x48, port, 4), (MC + 0x4C, 0, 4), (MC + 0x50, 0, 4), (MC + 0x54, 0, 4), (MC + 0x58, 0, 4),
         (MC + 0x5C, 0, 4), (MC + 0x60, 0, 4), (MC + 0x64, 0, 4), (MC + 0x68, count, 4)]
    b += [(MC + 0x6C + n, present[n] if n < len(present) else 0, 1) for n in range(8)]
    return b


def helpers(err=0, phase=0, last=0):
    return L.helpers(err, phase, last)


def records(beat, present):
    """Slot records +0x74 + 12 n for the row renderer (as the enumeration
    stores them from saved(): area, place, glyph choice, play time)."""
    out = []
    for n in range(5):
        r = MC + 0x74 + 12 * n
        out += [(r, PLACES[n], 1), (r + 1, n % 2, 1), (r + 6, n & 1, 1), (r + 8, TIMES[n], 4)]
    return out


def pads(e74=0, e78=0):
    return [(E74, e74, 2), (E78, e78, 2)]


def one_case(item):
    group, beat, knobs = item
    S.CARD = knobs.get('card', {})
    ram, spad = S.make(beat, knobs)
    steps = [('call', knobs['fn'], tuple(knobs.get('args', (MC,))))] * knobs.get('frames', 1)
    return run_both(item, bytes(spad), steps)


# ---- unit items -----------------------------------------------------------------------

PRESSES = (0, 0x40, 0x20, 0x10, 0x2000, 0x8000)


def gate(kind):
    """The helpers' state for a GetInfo poll's outcome (the Sync before it
    done): 'issue' the poll issued (-1), 'ok' result 0, 'new' result -1
    (the SDK's: -2 back), 'bad' result 5, 'err' the error flag set."""
    return {'issue': helpers(), 'ok': helpers(0, 1, 0), 'new': helpers(0, 1, NEG), 'bad': helpers(0, 1, 5),
            'err': helpers(1, 1, 0)}[kind]


def unit_items():
    out = []
    for beat in ('o7', 'd5'):
        b0 = records(beat, ())
        # 002280F0: each state, the poll kinds, type / format / free / retry, GetDir's results
        for st in (0, 2):
            for kind in ('issue', 'ok', 'new', 'bad', 'err'):
                for typ, fmt, retry in ((2, 1, 0), (0, 1, 0), (2, 0, 0), (2, 0, 6), (2, 0, 5)):
                    if kind != 'ok' and (typ, fmt, retry) != (2, 1, 0):
                        continue
                    for mode in ((1, 2) if st == 0 else (1,)):
                        for free in ((8000, 0) if st == 2 and kind == 'ok' and fmt == 1 else (8000,)):
                            b = rec(mode, 0, step=st, retry=retry) + gate(kind) + [(MC + 0x58, typ, 4),
                                                                                    (MC + 0x60, fmt, 4),
                                                                                    (MC + 0x5C, free, 4)]
                            out.append(('unit', beat, {'fn': 0x2280F0, 'b': b + b0}))
        for mode in (1, 2):
            for last in (0, 1, NEG):
                out.append(('unit', beat, {'fn': 0x2280F0, 'b': rec(mode, 1, step=1) + helpers(0, 0xD, last)}))
            out.append(('unit', beat, {'fn': 0x2280F0, 'b': rec(mode, step=1) + helpers()}))
        out.append(('unit', beat, {'fn': 0x2280F0, 'b': rec(step=3) + helpers()}))
        out.append(('unit', beat, {'fn': 0x2280F0, 'b': rec(step=3) + helpers(1)}))
        out.append(('unit', beat, {'fn': 0x2280F0, 'b': rec(step=0) + helpers(), 'card': {'pending': 1, 'busy': 1}}))
        # 00228320 over free counts around the bound
        for free in (0, 18, 86, 87, 88, 89, 0x80, 8000, 0x7FFFFFFF, NEG):     # the bound: 18 + 70 KiB of icon
            out.append(('unit', beat, {'fn': 0x228320, 'b': [(MC + 0x5C, free, 4)]}))
        # 002293B0 over presence patterns and record kinds
        for present in ((0, 0, 0, 0, 0), (1, 1, 1, 1, 1), (2, 1, 0, 2, 1), (1, 0, 2, 0, 1)):
            out.append(('unit', beat, {'fn': 0x2293B0, 'b': rec(present=present) + b0}))
        # negative and boundary play times (the divisions' signs)
        times = [(MC + 0x74 + 12 * n + 8, t, 4) for n, t in enumerate((0x80000000, NEG, 215999, 216000, 3599))]
        out.append(('unit', beat, {'fn': 0x2293B0, 'b': rec(present=(1, 1, 1, 1, 1)) + b0 + times}))
        # the cursor 00229170: modes, presses, repeats, the cursor's bounds, the poll
        for mode in (1, 2):
            for count in ((0, 2) if mode == 2 else (0,)):
                for press in PRESSES:
                    for chosen, present in ((0, (1, 0, 0, 0, 0)), (2, (1, 0, 0, 0, 0)), (4, (0, 0, 0, 0, 2))):
                        out.append(('unit', beat, {'fn': 0x229170, 'b': rec(mode, chosen=chosen, present=present,
                                                                            count=count) + pads(press) + gate('ok')}))
        for chosen in (0, 2, 4):
            for rep in (0x1000, 0x4000, 0x5000):
                out.append(('unit', beat, {'fn': 0x229170, 'b': rec(2, chosen=chosen) + pads(0, rep) + gate('ok')}))
        for kind in ('issue', 'new', 'bad', 'err'):
            out.append(('unit', beat, {'fn': 0x229170, 'b': rec(2) + gate(kind)}))
        out.append(('unit', beat, {'fn': 0x229170, 'b': rec(2) + gate('ok') + [(MC + 0x58, 2, 4)],
                                   'card': {'types': [0]}}))
        out.append(('unit', beat, {'fn': 0x229170, 'b': rec(2) + helpers(), 'card': {'pending': 1, 'busy': 1}}))
        # the confirmation 00227980
        for mode, present in ((1, (0,)), (1, (1,)), (1, (2,)), (1, (3,)), (2, (1,))):
            for st3 in (0, 1, 2):
                for yes in (0, 1, 2):
                    for press in PRESSES:
                        if st3 != 1 and press:
                            continue
                        if mode == 2 and present != (1,):
                            continue
                        out.append(('unit', beat, {'fn': 0x227980, 'b': rec(mode, st3=st3, yes=yes, present=present)
                                                   + pads(press) + gate('ok')}))
        for kind in ('issue', 'new', 'bad', 'err'):
            out.append(('unit', beat, {'fn': 0x227980, 'b': rec(2, st3=1) + gate(kind)}))
        out.append(('unit', beat, {'fn': 0x227980, 'b': rec(2, st3=1) + helpers(), 'card': {'pending': 1, 'busy': 1}}))
        out.append(('unit', beat, {'fn': 0x227980, 'b': rec(2, st3=1, yes=1) + pads(0x2000 | 0x8000) + gate('ok')}))
        # the messages 00227010 / 00227160 and 00227270
        for mode, present in ((1, (2,)), (1, (1,)), (2, (0,))):
            for st3, frames in ((0, 0), (1, 5), (1, 1), (2, 1)):
                out.append(('unit', beat, {'fn': 0x227010, 'b': rec(mode, st3=st3, present=present, frames=frames)}))
            for press in PRESSES[:4]:
                out.append(('unit', beat, {'fn': 0x227160, 'b': rec(mode, present=present) + pads(press)}))
        for press in PRESSES[:4]:
            out.append(('unit', beat, {'fn': 0x227270, 'args': (), 'b': pads(press)}))
        # 00227DE0 / 00228070 / 001FED40 / 00227C60
        for st4 in (0, 1, 2, 3):
            for yes in (0, 1):
                for press in PRESSES:
                    if st4 in (0, 3) and press not in (0, 0x20):
                        continue
                    out.append(('unit', beat, {'fn': 0x227DE0, 'b': rec(1, st4=st4, yes=yes) + pads(press) +
                                               gate('ok')}))
        for kind in ('issue', 'new', 'bad', 'err'):
            out.append(('unit', beat, {'fn': 0x227DE0, 'b': rec(1, st4=1) + gate(kind)}))
        out.append(('unit', beat, {'fn': 0x227DE0, 'b': rec(1, st4=1) + gate('ok'), 'card': {'types': [3]}}))
        out.append(('unit', beat, {'fn': 0x227DE0, 'b': rec(1, st4=1) + helpers(), 'card': {'pending': 1, 'busy': 1}}))
        for phase, last, err in ((0, 0, 0), (0x10, 0, 0), (0x10, 3, 0), (7, 0, 0), (0x10, 0, 1)):
            for v0 in ((0, 1) if phase == 0 else (0,)):
                out.append(('unit', beat, {'fn': 0x228070, 'b': rec(1) + helpers(err, phase, last),
                                           'card': {'issue': {0: v0}} if v0 else {}}))
                out.append(('unit', beat, {'fn': 0x1FED40, 'args': (1, 0), 'b': helpers(err, phase, last),
                                           'card': {'issue': {0: v0}} if v0 else {}}))
        out.append(('unit', beat, {'fn': 0x228070, 'b': rec(1) + helpers(), 'card': {'pending': 1, 'busy': 1}}))
        for st3 in (0, 1, 2, 3, 4, 7):
            for v in ((0x40, 0), (0x20, 0), (0x10, 0), (0, 0)):
                out.append(('unit', beat, {'fn': 0x227C60, 'b': rec(1, st3=st3, st4=1) + pads(*v) + gate('ok')}))
        out.append(('unit', beat, {'fn': 0x227C60, 'b': rec(1, st3=1, st4=2, yes=1) + pads(0x40) + gate('ok')}))
        out.append(('unit', beat, {'fn': 0x227C60, 'b': rec(1, st3=1, st4=1) + gate('bad')}))
        out.append(('unit', beat, {'fn': 0x227C60, 'b': rec(1, st3=2) + helpers(0, 0x10, 0)}))
        out.append(('unit', beat, {'fn': 0x227C60, 'b': rec(1, st3=2) + helpers(0, 0x10, 4)}))
        # 00226E90 both modes
        for st3, st4, yes, press in ((1, 2, 1, 0x40), (1, 1, 0, 0x20), (1, 1, 0, 0x10), (0, 0, 0, 0), (2, 0, 0, 0)):
            out.append(('unit', beat, {'fn': 0x226E90, 'b': rec(1, st3=st3, st4=st4, yes=yes) + pads(press) +
                                       gate('ok') + b0}))
        out.append(('unit', beat, {'fn': 0x226E90, 'b': rec(1, st3=2) + helpers(0, 0x10, 0) + b0}))
        for press, kind in ((0x40, 'ok'), (0x20, 'ok'), (0x10, 'ok'), (0, 'bad'), (0, 'ok')):
            out.append(('unit', beat, {'fn': 0x226E90, 'b': rec(2, present=(1,)) + pads(press) + gate(kind) + b0}))
        # 00226B90 / 00226D50
        for st3 in (0, 1, 2, 3):
            for press in PRESSES[:4]:
                out.append(('unit', beat, {'fn': 0x226B90, 'b': rec(1, st3=st3) + pads(press) + gate('ok')}))
        for fn in (0x226B90, 0x226D50):
            for kind in ('issue', 'new', 'bad', 'err'):
                out.append(('unit', beat, {'fn': fn, 'b': rec(2, st3=3) + gate(kind)}))
            out.append(('unit', beat, {'fn': fn, 'b': rec(2, st3=3) + gate('ok'), 'card': {'types': [0]}}))
            out.append(('unit', beat, {'fn': fn, 'b': rec(2, st3=3) + helpers(), 'card': {'pending': 1, 'busy': 1}}))
        for press in PRESSES[:4]:
            out.append(('unit', beat, {'fn': 0x226D50, 'b': rec(2) + pads(press) + gate('ok')}))
        # 002267A0: both ports chosen, both turns, every poll result and type
        for port in (0, 1):
            for poll in (0, 1, 2):
                for kind in ('issue', 'ok', 'new', 'bad'):
                    for typ in (2, 0):
                        if kind in ('issue', 'bad') and typ == 0:
                            continue
                        if poll == 2 and kind != 'ok':
                            continue
                        out.append(('unit', beat, {'fn': 0x2267A0, 'b': rec(2, port, poll=poll) + gate(kind) +
                                                   [(MC + 0x58, typ, 4)], 'card': {'types': [typ]}}))
                out.append(('unit', beat, {'fn': 0x2267A0, 'b': rec(2, port, poll=poll) + helpers(0, 1, 0xFFFFFFFE) +
                                           [(MC + 0x58, 2, 4)]}))
        for press in PRESSES[:4]:
            out.append(('unit', beat, {'fn': 0x2267A0, 'b': rec(2, 1) + pads(press) + gate('ok')}))
        out.append(('unit', beat, {'fn': 0x2267A0, 'b': rec(2) + helpers(1, 1, 0)}))
        out.append(('unit', beat, {'fn': 0x2267A0, 'b': rec(2) + helpers(), 'card': {'pending': 1, 'busy': 1}}))
        # 00227300 one frame on each screen / step (the children's own cases above)
        for mode in (1, 2):
            for port in (0, 1, 2):
                for screen, step in ((0, 0), (1, 0), (2, 0), (2, 1), (2, 3), (2, 4), (2, 5), (3, 0), (4, 0), (4, 1),
                                     (4, 2), (4, 3), (4, 4), (5, 0)):
                    if port == 2 and (screen, step) != (0, 0):
                        continue
                    out.append(('unit', beat, {'fn': 0x227300, 'b': rec(mode, port, screen, step, present=(1, 2)) +
                                               gate('issue') + b0}))
        # the states the cases above leave out: GetInfo type 2 on the error
        # screens' tails, a Yes / No cursor and a presence byte outside their
        # values, 00227300 with its children's results 4 and 2
        for fn in (0x226B90, 0x226D50):
            out.append(('unit', beat, {'fn': fn, 'b': rec(2, st3=3) + gate('ok') + [(MC + 0x58, 2, 4)],
                                       'card': {'types': [2]}}))
        out.append(('unit', beat, {'fn': 0x227DE0, 'b': rec(1, st4=2, yes=2) + gate('ok')}))
        out.append(('unit', beat, {'fn': 0x2293B0, 'b': rec(present=(3, 1, 3, 0, 2)) + b0}))
        out.append(('unit', beat, {'fn': 0x227300, 'b': rec(2, 0, 1, 0) + helpers(1) + b0}))
        for step, st3, press in ((0, 0, 0), (1, 1, 0), (1, 1, 0x10)):
            card = {} if press else {'types': [0]}
            extra = [] if press else [(MC + 0x58, 0, 4)]
            out.append(('unit', beat, {'fn': 0x227300, 'b': rec(2, 0, 2, step, st3=st3, present=(1,)) + pads(press) +
                                       gate('ok') + extra + b0, 'card': card}))
    return out


# ---- whole screens ---------------------------------------------------------------------

PLACES = (0x0B, 0x01, 0x00, 0x15, 0x0B)          # area bytes the place tables resolve
TIMES = (0, 59, 216000 * 3 + 3600 * 59 + 30, 0x7FFFFFFF, 216000 * 99 + 3599)


def saved(block, tag):
    """A slot as the writer leaves it, its header bytes the row renderer
    reads made distinct and resolvable: byte 0 an area, 1 its place, 0xA the
    glyph choice, word 0x50 the play time (the other bytes the live
    block's: later frames' text reads the loaded block); +0xF = 1 and the
    check byte."""
    b = bytearray(block)
    b[0], b[1], b[0xA] = PLACES[tag], tag % 2, tag & 1
    b[0x50:0x54] = struct.pack('<I', TIMES[tag])
    b[0xF] = 1
    b[0x63F] = (0x100 - sum(b[:0x63F])) & 0xFF
    return bytes(b)


def card_files(beat, present, drop=()):
    block = L.live_block(beat)
    out = {DIR + '/' + DIR: bytes(0x48), DIR + '/EX.ico': bytes(range(256)) * 4, DIR + '/icon.sys': bytes(0x3C4)}
    for n in range(5):
        out[DIR + '/EX_DATA.%02d' % n] = saved(block, n) if n in present else L.blank(block)
    for d in drop:
        out.pop(DIR + '/' + d)
    return out


def policy(ee, goal, frame):
    """The pad words for the next frame from the screen's state (the oracle
    alone; both sides then get the same pokes). A press every other frame."""
    if frame & 1:
        return 0, 0
    b = lambda off: ee.load(MC + off, 1)  # noqa: E731
    screen, step, st3, st4, yes, cur = b(1), b(2), b(3), b(4), b(9), b(0xA)
    mode = b(0x14)

    def cursor():
        if cur < goal.get('slot', 0):
            return 0, 0x4000
        if cur > goal.get('slot', 0):
            return 0, 0x1000
        return goal.get('pick', 0x40), 0

    def confirm(want, active):
        if not active:
            return 0, 0
        if want and yes == 0:
            return 0x8000, 0
        if not want and yes != 0:
            return 0x2000, 0
        return 0x40, 0

    if screen == 2:
        if step == 0:
            return cursor()
        if step == 1:
            return confirm(goal.get('yes', True), st3 == 1)
        if step == 4:
            return goal.get('fail', 0x40), 0
        return 0, 0
    if screen == 4:
        if step == 0 and mode == 1:
            if st3 == 1:
                if st4 == 1:
                    return goal.get('format_first', 0x40), 0
                if st4 == 2:
                    return confirm(goal.get('format', True), True)
                return 0, 0
            if st3 == 4:
                return goal.get('err', 0x40), 0
            return 0, 0
        if step == 0:
            return cursor()
        if step == 1 and st3 == 1:
            return 0x40, 0
        if step == 2 and frame < goal.get('wait', 0):
            return 0, 0
        return goal.get('err', 0x40), 0
    return 0, 0


def flow_items():
    """Whole card screens; 'want' is 00227300's result that ends each."""
    out = []
    for beat in ('o7', 'd5'):
        def add(want, **k):
            out.append(('flow', beat, dict(k, want=want)))
        # LOAD GAME: each slot, both ports
        for slot in range(5):
            for port in (0, 1):
                if port and slot not in (1, 4):
                    continue
                add(3, mode=2, port=port, present=(0, 1, 2, 3, 4), goal={'slot': slot})
        add(3, mode=2, present=(1, 3), goal={'slot': 3}, card={'busy': 1})
        add(2, mode=2, present=(1, 3), goal={'slot': 1, 'yes': False, 'then': 0x10})      # No, then Triangle
        add(1, mode=2, present=(2,), goal={'slot': 0, 'pick': 0x20})                       # Circle on the list
        add(2, mode=2, present=(2,), goal={'slot': 0, 'pick': 0x10})                       # Triangle on the list
        add(2, mode=2, present=None, goal={'slot': 0, 'pick': 0x10})                       # no directory
        add(1, mode=2, present=(2,), drop=('icon.sys',), goal={'err': 0x40})               # the card refused
        add(2, mode=2, present=(2,), drop=('icon.sys',), goal={'err': 0x10})
        add(1, mode=2, present=(2,), goal={'slot': 2}, card={'fail_read': True})           # a failing read
        add(2, mode=2, present=(2,), goal={'slot': 2, 'err': 0x20}, card={'types': [0]})   # no card of type 2
        add(1, mode=2, port=1, present=(2,), goal={'slot': 2, 'wait': 200},                # the card back
            card={'types': [0, 0, 0, 0, 0, 2]})
        add(2, mode=2, present=(2,), goal={'slot': 0, 'pick': 0x10}, card={'formats': [0]})
        # SAVE: a new card, an existing directory, both ports
        for slot in (0, 3):
            for port in (0, 1):
                add(3, mode=1, port=port, present=None, goal={'slot': slot})
                add(3, mode=1, port=port, present=(1, 3), goal={'slot': slot})
        add(1, mode=1, present=(1,), goal={'slot': 1, 'yes': False, 'then': 0x20})          # No, then Circle
        add(1, mode=1, present=None, goal={'slot': 2}, card={'frees': [8000, 5]})          # too full
        add(1, mode=1, present=None, goal={'slot': 2, 'err': 0x20}, card={'frees': [8000, 5]})
        add(3, mode=1, present=None, goal={'slot': 0}, card={'formats': [0]})              # format, then save
        add(1, mode=1, present=None, goal={'slot': 0, 'format': False}, card={'formats': [0]})
        add(2, mode=1, present=None, goal={'slot': 0, 'format_first': 0x10}, card={'formats': [0]})
        add(1, mode=1, present=None, goal={'slot': 0}, card={'formats': [0], 'format_result': -1})
        add(1, mode=1, present=None, goal={'slot': 1}, card={'fail_write': True})          # a failing write
        add(1, mode=1, present=(2,), goal={'slot': 2, 'pick': 0x20})
        # an unusable slot (presence 2) in save mode: 00229680 writes the New
        # Game block, the card check runs again, then the save; and its failure
        add(3, mode=1, present=None, goal={'slot': 0}, start={'screen': 2, 'step': 2, 'present': (2,)})
        add(1, mode=1, present=None, goal={'slot': 0}, start={'screen': 2, 'step': 2, 'present': (2,)},
            card={'fail_write': True})
        # a failing command in the load enumeration (its 2)
        add(2, mode=2, present=(1, 2), goal={'slot': 1, 'err': 0x20}, card={'result': {19: -4}})   # its GetInfo
    return out


FLOW_FRAMES = 600


def flow_setup(item):
    group, beat, knobs = item
    present = knobs['present']
    files = {} if present is None else card_files(beat, set(present), knobs.get('drop', ()))
    cfg = dict(knobs.get('card', {}), files=files)
    S.CARD = cfg
    st = knobs.get('start', {})
    b = rec(knobs['mode'], knobs.get('port', 0), st.get('screen', 0), st.get('step', 0),
            present=st.get('present', ())) + helpers() + records(beat, ())
    ram, spad = S.make(beat, {'b': b})
    return cfg, files, bytes(spad)


def flow_steps(item, spad, cfg):
    """The oracle alone: the pad script and the frame of 00227300's first
    nonzero result."""
    goal = item[2]['goal']
    U.WSPEC = {a: spec for a, spec in S.WSPEC.items() if a not in S.LANE}
    ee = U.oracle_ee(spad)
    oracle_hooks(ee)
    steps = []
    then, confirming = None, False
    packet = ee.load(PACKET)
    for frame in range(FLOW_FRAMES):
        e74, e78 = policy(ee, goal, frame)
        if then is not None:
            e74, e78 = (0, 0) if frame & 1 else (then, 0)
        for at, v, n in ((E74, e74, 2), (E78, e78, 2), (PACKET, packet, 4)):
            ee.save(at, v, n)
            steps.append(('poke', at, v, n))
        ee.r[4], ee.r[29], ee.r[31] = MC, STACK_TOP, RETURN
        ee.run(0x227300)
        steps.append(('call', 0x227300, (MC,)))
        if ee.r[2] & MASK:
            return steps, ee.r[2] & MASK, ee
        # a declined confirmation, back on the cursor: the goal's exit press
        on_list = ee.load(MC + 1, 1) == 2
        if on_list and ee.load(MC + 2, 1) == 1:
            confirming = True
        elif on_list and ee.load(MC + 2, 1) == 0 and confirming and goal.get('yes') is False:
            then = goal.get('then', 0x10)
    return steps, 0, ee


class FailCard(CardModel):
    """A card whose slot reads (fail_read) or whose first slot write
    (fail_write) are refused with -4."""

    def serve(self, mem, address, args, k):
        if address == READ and self.cfg.get('fail_read') and self.fds.get(args[0] & MASK, '').endswith('.02') and \
                self.loaded:
            self.order.append(('read', 'refused', 0))
            return -4
        if address == WRITE and self.cfg.get('fail_write') and 'EX_DATA' in self.fds.get(args[0] & MASK, ''):
            self.order.append(('write', 'refused', 0))
            return -4
        return super().serve(mem, address, args, k)

    @property
    def loaded(self):
        # the enumeration's reads are done once GetInfo after them was asked
        return sum(1 for o in self.order if o[0] == 'getinfo') >= 2


def flow_case(item):
    group, beat, knobs = item
    cfg, files, spad = flow_setup(item)
    steps, value, oracle = flow_steps(item, spad, cfg)
    goal = knobs['goal']
    assert value == knobs['want'], (item, 'the card screen ended', value, 'not', knobs['want'],
                                    [hex(oracle.load(MC + i, 1)) for i in range(1, 5)])
    failing = cfg.get('fail_read') or cfg.get('fail_write') or knobs.get('drop') or cfg.get('types') or \
        cfg.get('formats') or cfg.get('frees') or cfg.get('result') or 'start' in knobs
    done = value == 3
    slot = goal.get('slot', 0)
    model = S.MODELS['oracle']
    if not failing and goal.get('yes', True) and goal.get('pick', 0x40) == 0x40:
        present = knobs['present']
        if knobs['mode'] == 2 and present is not None and slot in present:
            assert done, (item, 'LOAD GAME did not complete', value)
        if knobs['mode'] == 1:
            assert done, (item, 'the save did not complete', value)

    def check(ee):
        name = DIR + '/EX_DATA.%02d' % slot
        if done and knobs['mode'] == 2:
            assert bytes(ee.mem[BLOCK:BLOCK + 0x640]) == files[name], (item, 'D_00810700 is not the slot file')
        if done and knobs['mode'] == 1:
            live = bytes(ee.mem[BLOCK:BLOCK + 0x640])
            assert model.files[name] == live, (item, 'the slot written is not the live block')
            assert sum(live) & 0xFF == 0 and live[0xF] == 1, (item, 'check byte')
            start = struct.unpack_from('<h', R.IMAGES[beat][0], CB2)[0]
            now = struct.unpack_from('<h', bytes(ee.mem), CB2)[0]
            want = start if 'start' in knobs else start - 4      # 00229680's path gives its 4 back first
            assert now == want, (item, 'D_00810CB2 after a save', start, now)
            if knobs['present'] is None:
                assert sorted(model.files) == sorted(DIR + '/' + f for f in L.FILES), (item, sorted(model.files))
        return True
    r = run_both(item, spad, steps, check=check)
    assert S.MODELS['oracle'].order == S.MODELS['native'].order, (item, 'command order differs')
    assert S.MODELS['oracle'].files == S.MODELS['native'].files, (item, 'cards differ')
    return r


def case_groups():
    return [('unit', unit_items(), one_case), ('flow', flow_items(), flow_case)]


# Branch outcomes no input reaches. A GetInfo poll result other than 0 and
# -1 always comes with the error flag set (001FECB0 sets it), which 00226B90,
# 00226D50, 00227980 and 00229170 test first, so their -1 test never sees
# another value; each switch over a callee's result has an arm for a value
# the callee never returns (00227C60 and 00227DE0 return 0..3, 00229170
# 0..4, 002280F0 0..4, 002283B0 0..3, 00227980 0..4, 00228A90 / 00228B60 /
# 00229680 0..2); 002280F0's second type test repeats the first (type 2
# known).
UNREACHED = {
    (0x226D28, True), (0x226E6C, True), (0x227C38, True), (0x229384, True),
    (0x226ED8, False), (0x226F90, False), (0x227404, False), (0x2274A8, False), (0x22762C, False),
    (0x227848, False), (0x2277BC, False), (0x227CE8, False), (0x228184, True), (0x228280, True),
}

# The default run: the greedy branch-outcome cover of the full sweep (the
# `cover` command prints it; item indices into each group's deterministic
# list), plus the decodes: LOAD GAME of slot 0 (flow 0, D_00810700 equal to
# the slot file) and the saves to a new card and over an existing directory
# (flows 18, 19, the card checked); the cases added for the outcomes the
# first sweep missed (unit 498..505, flows 35..37).
QUICK = {
    'flow': [0, 4, 7, 8, 9, 12, 14, 16, 17, 18, 19, 22, 27, 32, 33, 35, 36, 37],
    'unit': [12, 21, 23, 24, 26, 33, 37, 77, 109, 112, 116, 131, 138, 145, 167, 177, 202, 245, 252, 253, 257, 264,
             276, 277, 279, 295, 298, 304, 305, 306, 315, 318, 324, 328, 341, 349, 354, 359, 362, 363, 364, 365, 366,
             370, 371, 380, 385, 389, 391, 394, 395, 398, 399, 400, 402, 403, 405, 406, 407, 408, 409, 410, 411, 412,
             413, 414, 420, 422, 423, 425, 427, 428, 429, 430, 431, 435, 436, 437, 438, 439, 446, 447, 452, 453, 468,
             477, 498, 499, 500, 501, 502, 503, 504, 505],
}


def decode(item):
    """Cases every run keeps beside the cover: the free count's bound
    (00228320), the rows' text (002293B0) and the load-mode Cross on each
    kind of slot (00229170), whose stores and results the cover's branch
    outcomes do not pin."""
    group, beat, knobs = item
    if group != 'unit' or beat != 'o7':
        return False
    if knobs['fn'] in (0x228320, 0x2293B0):
        return True
    b = dict((at, v) for at, v, _ in knobs.get('b', ()))
    return knobs['fn'] == 0x229170 and b.get(MC + 0x14) == 2 and b.get(E74) == 0x40


def sample(items, name):
    if RM.FULL or name not in QUICK:
        return items
    keep = set(QUICK[name]) | {i for i, it in enumerate(items) if decode(it)}
    return [items[i] for i in sorted(keep)]


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
            per.append((name, i, frozenset(o for o in r[0] if o[0] in CARD_PCS)))
        counts.append(RM.part(len(chosen), len(items), name + ' cases'))
    return per, outcomes, cases, calls, entries, counts


CARD_PCS = frozenset(pc for a, n in CARD_LANE.items() for pc in range(a, a + n, 4))


def card_branches():
    return {pc for pc in S.all_branches() if pc in CARD_PCS}


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
    for must, idx in (('flow', {0, 18, 19}),):      # the decodes, every run: a load, a save to a new card and over a directory
        chosen[must] = sorted(set(chosen.get(must, [])) | idx)
    print('QUICK = {')
    for name in sorted(chosen):
        print('    %r: %r,' % (name, sorted(chosen[name])))
    print('}')


def main():
    t0 = time.time()
    setup()
    _, outcomes, cases, calls, entries, counts = with_pool(lambda: run_groups(RM.FULL))
    branches = card_branches()
    need = {(pc, t) for pc in branches for t in (True, False)}
    got = {o for o in outcomes if o[0] in CARD_PCS}
    missing = sorted((hex(pc), t) for pc, t in need - got if (pc, t) not in UNREACHED)
    RM.banner(*counts, f'{cases} cases', f'{calls} callee calls, {entries} entries with RAM + scratchpad + '
              'arguments equal to the original\'s',
              f'{len(branches)} conditional branches of the card screen: {len(need & got)} of {len(need)} '
              'outcomes seen')
    if RM.FULL:
        assert not missing, ('branch outcomes never seen', missing)
    elif missing:
        print('outcomes not in this sample (EM_TEST_FULL=1 requires all):', len(missing))
    if missing and os.environ.get('EM_SAVE_SHOW_MISSING'):
        print('missing:', missing)
    print('elapsed %.1f s' % (time.time() - t0))
    print('card screen reference: PASS')


def debug(names):
    setup()
    table = {name: (items, fn) for name, items, fn in case_groups()}
    for name in names:
        group, _, idx = name.partition(':')
        items, fn = table[group]
        chosen = items if not idx else [items[int(i)] for i in idx.split(',')]
        for i, it in zip(idx.split(',') if idx else range(len(chosen)), chosen):
            t = time.time()
            try:
                r = fn(it)
                print('ok', i, repr(it)[:160], 'calls', r[1] if r not in ('unmeasured', 'skipped') else r,
                      '%.2fs' % (time.time() - t))
            except AssertionError as ex:
                print('FAIL', i, repr(it)[:200], repr(ex)[:3000])


if __name__ == '__main__':
    _ = random
    if len(sys.argv) > 1 and sys.argv[1] == 'debug':
        debug(sys.argv[2:])
    elif len(sys.argv) > 1 and sys.argv[1] == 'cover':
        cover()
    else:
        main()
