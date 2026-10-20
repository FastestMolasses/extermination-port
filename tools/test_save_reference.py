#!/usr/bin/env python3
"""Execute the ORIGINAL memory-card save writer and compare the native
em_save_* translations byte for byte; decode the save's card layout from the
original's own output.

docs/SAVE_GAME.md. The user's pinned ELF and two recorded images (the fork's
c10/options/opt_07_load_cancel, the options' load screen in AREA11, and
s87/route_a01/a01_s4_east_room, the AREA01 room of the nearest save
terminal; eeMemory.bin and scratchpad.bin in the reference
tools/reference_root.py selects) supply every instruction and every table;
none are embedded here.

Method: the options lane's (tools/test_options_reference.py, on the AREA01
UI lane's interpreter, dirty-page tracker, entry check and run_both). The
oracle runs the original routine over a captured image; every callee outside
the lane runs as ORIGINAL code too (block_copy 00121870, 00121A28, 00123168
and New Game's reset 001AF2C0), nested, its entry logged. The native module
runs over a second copy; each callee it reaches runs the same original code
in a second interpreter sharing its memory. At every callee entry all of RAM
and the scratchpad must equal the oracle's at the same call; at the end the
results, the call log, RAM and scratchpad must be identical. 001FE9A0 (the
Sync poll) is in the lane as its options translation.

Scripted boundary (both sides, entry checked and logged, not run): the
libmc SDK the writer reaches, Open 00114360, Mkdir 00114448, Close 00114480,
Write 00114708, GetDir 00114AC0 and Sync 00114848. A small card model
answers them identically on both sides (the IOP's card server is not EE
code: these answers are the test's inputs, not claims about the server): a
command is issued with v0 0 (or a scripted nonzero v0), Sync returns 1 with
the command's number and result stored through its pointers (after a
scripted number of busy 0 returns), -1 with no command pending; Open gives
a descriptor, Write stores the bytes it is handed and returns their count,
Mkdir / Close 0, GetDir the number of entries (it writes no table). A
case can script a result (a failure) or a short write at the k-th command.

Cases: every command routine and the file writer on every phase and result;
the block's check byte and 001AF220 over both images; each writer step and
state with the results that move it; whole saves, frame by frame, from
00228A90's first call to its result (a new directory and an existing one,
every slot, both ports, busy Syncs, a failure at every command), and the
New Game block's slot write 00229680. For the whole saves the card the
original wrote is checked against docs/SAVE_GAME.md section 2 (the layout
claims are the original's output, read back from the model).

EM_TEST_FULL=1 runs every case; the default a covering sample. Up to four
worker processes (EM_TEST_JOBS=1: serial). Commands: `debug <group>[:i,..]`,
`cover`.
"""
import ctypes as C
import multiprocessing
import os
import struct
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
os.environ.setdefault('EM_LANE', 'save')
import reference_mode as RM  # noqa: E402
import reference_root as refs  # noqa: E402
import test_area01_render_reference as R  # noqa: E402
import test_area01_ui_reference as U  # noqa: E402
from test_player_slide_reference import EE, read_elf  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
OUT = ROOT / 'build' / 'save' / 'reference'
R.OUT = U.OUT = OUT
JOBS = R.JOBS

IMAGES = {
    'o7': refs.capture('c10') / 'options' / 'opt_07_load_cancel',
    'a4': refs.capture('s87/route_a01') / 'a01_s4_east_room',
}
BEATS = list(IMAGES)

LANE = {
    0x228A90: 0xCC, 0x228530: 0x68, 0x2285A0: 0x2F8, 0x2288A0: 0x1E8, 0x229640: 0x38, 0x229680: 0xE4,
    0x2284F0: 0x3C, 0x1AF220: 0x9C, 0x1FE6B0: 0x104, 0x1FE9E0: 0x88, 0x1FEB90: 0x88, 0x1FEA70: 0x88,
    0x1FEDD0: 0x88, 0x1FEC20: 0x88, 0x1FE9A0: 0x38,
}
TRPC = frozenset(pc for a, n in LANE.items() for pc in range(a, a + n, 4))

OPEN, MKDIR, CLOSE, WRITE, GETDIR, SYNC = 0x114360, 0x114448, 0x114480, 0x114708, 0x114AC0, 0x114848
SDK = {OPEN: 2, CLOSE: 3, WRITE: 6, MKDIR: 0xB, GETDIR: 0xD}   # the command numbers the helpers' phases use

# callee -> (integer argument registers, float argument registers, result used)
WSPEC = {
    OPEN: (4, 0, 'v0'), MKDIR: (3, 0, 'v0'), CLOSE: (1, 0, 'v0'), WRITE: (3, 0, 'v0'), GETDIR: (6, 0, 'v0'),
    SYNC: (3, 0, 'v0'), 0x121870: (3, 0, None), 0x121A28: (3, 0, None), 0x123168: (2, 0, None),
    0x1AF2C0: (0, 0, None),
}
STUBS = {OPEN, MKDIR, CLOSE, WRITE, GETDIR, SYNC}

P, U8, U32, I32, U64 = C.POINTER, C.c_uint8, C.c_uint32, C.c_int32, C.c_uint64
ENTRIES = {  # native entry -> (argument ctypes, has a v0 result)
    0x228A90: ([U32], True), 0x228530: ([U32], False), 0x2285A0: ([U32], False), 0x2288A0: ([U32], False),
    0x229640: ([U32], True), 0x229680: ([U32], True), 0x2284F0: ([U32], False), 0x1AF220: ([], False),
    0x1FE6B0: ([U64] * 6, True), 0x1FE9E0: ([U64] * 4, True), 0x1FEB90: ([U64] * 3, True),
    0x1FEA70: ([U64], True), 0x1FEDD0: ([U64] * 3, True), 0x1FEC20: ([U64] * 6, True),
}
SOURCES = ['src/game/em_save_original.c', 'src/game/em_options_original.c', 'src/game/em_startup_load_gaps.c']
CFLAGS = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-ffp-contract=off', '-shared', '-fPIC',
          '-Isrc']
ELF = NATIVE = None
POOL = None

MC = 0x810040                      # D_00810040: the card screen's record
BLOCK = 0x810700                   # D_00810700: the progress block
C58, C5C, C64, C68, FD = 0x275C58, 0x275C5C, 0x275C64, 0x275C68, 0x275840
DIR_NAME, TITLE, HEADER_NAME, ICONSYS_NAME, ICONSYS = 0x2738E0, 0x2738F8, 0x273910, 0x273940, 0x266B30
ICON_NAME, SLOT_NAMES, ICON_PTR = 0x2739B0, 0x267060, 0x28A4A0


def build_native():
    import hashlib
    import subprocess
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('save' + ('.dylib' if sys.platform == 'darwin' else '.so'))
    h = hashlib.sha256(repr(CFLAGS).encode())
    for q in list(SOURCES) + sorted(str(x) for x in (ROOT / 'src').rglob('*.h')):
        h.update(str(q).encode())
        h.update((ROOT / q).read_bytes())
    stamp = lib.with_suffix(lib.suffix + '.sha256')
    if not (lib.exists() and stamp.exists() and stamp.read_text() == h.hexdigest()):
        subprocess.run(['cc'] + CFLAGS + list(SOURCES) + ['-o', str(lib)], cwd=ROOT, check=True)
        stamp.write_text(h.hexdigest())
    native = C.CDLL(str(lib))
    for address, (args, result) in ENTRIES.items():
        fn = getattr(native, 'em_save_%08X' % address)
        fn.argtypes = [P(U.UiState)] + args + ([P(U32)] if result else [])
        fn.restype = C.c_int
    return native


# ---- the card model (the scripted boundary) -------------------------------

CARD = {}            # the case's card configuration (see Card)
MODELS = {}          # 'oracle' / 'native' -> the Card of the last run on that side


def cstr(mem, at):
    at &= 0x1FFFFFF
    end = mem.find(b'\0', at, at + 0x100)
    return bytes(mem[at:end if end >= 0 else at + 0x100]).decode('latin-1')


class Card:
    """One side's card. CARD keys: 'dirs' (names already on the card),
    'busy' (Sync's 0 returns before each completion), 'issue' {k: v0} (the
    k-th issued command's nonzero v0: refused), 'result' {k: value} (the
    k-th command's result), 'short' {k: count} (the k-th command, a Write,
    stores and reports `count` bytes), 'pending' (a command number already
    pending at the start, its result 0)."""

    def __init__(self, cfg):
        self.cfg = cfg
        self.dirs = set(cfg.get('dirs', ()))
        self.files, self.fds, self.order = {}, {}, []
        self.pending = (cfg['pending'], 0) if cfg.get('pending') else None
        self.busy = cfg.get('busy', 0) if self.pending else 0
        self.k = 0
        self.next_fd = 3

    def answer(self, mem, save, address, args):
        """v0 of the SDK call; `save(at, value, size)` stores through a pointer."""
        if address == SYNC:
            _, cmd, res = args
            if self.pending is None:
                return MASK64
            if self.busy:
                self.busy -= 1
                return 0
            number, result = self.pending
            self.pending = None
            save(cmd & MASK, number, 4)
            save(res & MASK, result & MASK, 4)
            return 1
        k, self.k = self.k, self.k + 1
        v0 = self.cfg.get('issue', {}).get(k, 0)
        if v0:
            return v0 & MASK64
        result = self.serve(mem, address, args, k)
        if k in self.cfg.get('result', {}):
            result = self.cfg['result'][k]
        self.pending = (SDK[address], result)
        self.busy = self.cfg.get('busy', 0)
        return 0

    def serve(self, mem, address, args, k):
        if address == MKDIR:
            name = cstr(mem, args[2])
            self.order.append(('mkdir', name))
            self.dirs.add(name)
            return 0
        if address == GETDIR:
            name = cstr(mem, args[2])
            self.order.append(('getdir', name))
            return sum(1 for d in self.dirs if d == name)
        if address == OPEN:
            name = cstr(mem, args[2])
            fd, self.next_fd = self.next_fd, self.next_fd + 1
            self.fds[fd] = name
            self.order.append(('open', name, args[3] & MASK))
            return fd
        if address == WRITE:
            fd, buf, size = args[0] & MASK, args[1] & MASK, args[2] & MASK
            count = self.cfg.get('short', {}).get(k, size)
            data = bytes(mem[buf & 0x1FFFFFF:(buf & 0x1FFFFFF) + count])
            self.files[self.fds.get(fd, '?%d' % fd)] = data
            self.order.append(('write', self.fds.get(fd, '?'), count))
            return count
        if address == CLOSE:
            self.order.append(('close', self.fds.pop(args[0] & MASK, '?')))
            return 0
        raise AssertionError(('no model for', hex(address)))


def stub_hook(e, address):
    spec = U.WSPEC[address]
    entry = U.entry_of(e, address, spec)
    e.entries.append((len(e.journal), entry))
    v0 = e.card.answer(e.mem, e.save, address, entry[2])
    e.calls.append(entry + (U.result_of(spec, v0, 0),))
    e.exits.append((len(e.journal), v0 & MASK64, 0))
    e.r[2], e.f[0] = v0 & MASK64, 0


BASE_ORACLE_HOOKS = U.oracle_hooks


def oracle_hooks(ee):
    BASE_ORACLE_HOOKS(ee)
    ee.card = MODELS['oracle'] = Card(CARD)
    for a in STUBS:
        ee.hooks[a] = lambda e, a=a: stub_hook(e, a)


class Native(U.Native):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.card = MODELS['native'] = Card(CARD)

    def dispatch(self, _c, target, sp, a, na, f, nf, v0, f0):
        if target not in STUBS:
            return super().dispatch(_c, target, sp, a, na, f, nf, v0, f0)
        spec = U.WSPEC.get(target, (na, nf, None))
        entry = (target, sp, tuple(a[i] for i in range(na)), tuple(f[i] for i in range(nf)))
        if not self.entry_ok(entry):
            return -1
        if self.fail_at is not None and len(self.log) == self.fail_at:
            self.log.append(entry + ('fail',))
            return -1
        value = self.card.answer(self.ee.mem, self.ee.save, target, entry[2])
        self.log.append(entry + (U.result_of(spec, value, 0),))
        v0[0] = value & MASK64
        f0[0] = 0
        return 0

    def call(self, address, args):
        types, result = ENTRIES[address]
        fn = getattr(U.NATIVE, 'em_save_%08X' % address)
        out = U32(0)
        rc = fn(C.byref(self.state), *args, *([C.byref(out)] if result else []))
        return rc, (out.value if result else None)


def oracle_regs(address, args):
    return list(args)


def run_both(where, spad, steps, check=None):
    U.WSPEC = {a: spec for a, spec in WSPEC.items() if a not in LANE}
    return U.run_both(where, spad, steps, check=check)


def install():
    U.TRPC = TRPC
    U.ENTRIES = dict(ENTRIES)
    U.Native = Native
    U.oracle_hooks = oracle_hooks
    U.oracle_regs = oracle_regs
    U.ELF = R.ELF = ELF
    U.NATIVE = NATIVE
    U.REPLAY = set()
    U.MEMO_ON = set()


def load_images():
    for name, d in IMAGES.items():
        if name not in R.IMAGES:
            R.IMAGES[name] = ((d / 'eeMemory.bin').read_bytes(), (d / 'scratchpad.bin').read_bytes())
    ref = R.IMAGES[BEATS[0]][0]
    for name in BEATS:
        img = R.IMAGES[name][0]
        R.DIFF[name] = frozenset(p for p in range(R.RAM_SIZE // R.PAGE)
                                 if img[p * R.PAGE:(p + 1) * R.PAGE] != ref[p * R.PAGE:(p + 1) * R.PAGE])


def lane_pcs():
    return TRPC


def callee_set():
    probe = EE(ELF)
    targets = set()
    for pc in sorted(lane_pcs()):
        word = probe.load(pc)
        if word >> 26 in (2, 3):
            target = (word & 0x3FFFFFF) << 2
            if target not in LANE:
                targets.add(target)
    return targets


def check_code():
    base = EE(ELF)
    for name in BEATS:
        ram = R.IMAGES[name][0]
        for start in sorted(set(LANE) | set(WSPEC)):
            size = LANE.get(start, 0x40)
            assert ram[start:start + size] == bytes(base.mem[start:start + size]), (name, hex(start))
        for start, size in ((ICONSYS, 0x3C4), (SLOT_NAMES, 0x140), (DIR_NAME, 0xF0)):
            assert ram[start:start + size] == bytes(base.mem[start:start + size]), (name, 'data', hex(start))


def all_branches():
    probe = EE(ELF)
    out = set()
    for pc in sorted(lane_pcs()):
        word = probe.load(pc)
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        if op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or (op == 17 and rs == 8):
            if not (op == 4 and rs == 0 and rt == 0):
                out.add(pc)
    return out


# ---- case helpers -----------------------------------------------------------

put = U.put


def make(beat, knobs):
    ram, spad = R.case_ram(beat), bytearray(R.IMAGES[beat][1])
    for at, v, size in knobs.get('b', ()):
        put(ram, at, v, size)
    for at, v, size in knobs.get('spad', ()):
        spad[at - 0x70000000:at - 0x70000000 + size] = (v & ((1 << 8 * size) - 1)).to_bytes(size, 'little')
    return ram, spad


def rec(port=0, slot_index=0, state=0, step=0, phase=0, k=0):
    """The record's fields the writer reads (+0x4C, the card slot, is 0)."""
    return [(MC + 3, state, 1), (MC + 4, step, 1), (MC + 5, 0, 1), (MC + 0xA, slot_index, 1),
            (MC + 0x48, port, 4), (MC + 0x4C, 0, 4), (MC + 0x64, phase, 4), (MC + 0x68, k, 4)]


def helpers(err=0, phase=0, last=0, count=0, fd=0xFFFFFFFF):
    return [(C58, err, 4), (C5C, phase, 4), (C64, last, 4), (C68, count, 4), (FD, fd, 4)]


def one_case(item):
    """A single call (or `frames` calls) of one routine."""
    group, beat, knobs = item
    global CARD
    CARD = knobs.get('card', {})
    ram, spad = make(beat, knobs)
    steps = [('call', knobs['fn'], tuple(knobs.get('args', ())))] * knobs.get('frames', 1)
    return run_both(item, bytes(spad), steps)


# ---- items ------------------------------------------------------------------

NEG = 0xFFFFFFFF


def cmd_items():
    out = []
    routines = ((0x1FE9E0, 2, (0, 0, DIR_NAME, 0x203), True), (0x1FEB90, 6, (3, BLOCK, 0x640), True),
                (0x1FEA70, 3, (3,), False), (0x1FEDD0, 0xB, (1, 0, DIR_NAME), False),
                (0x1FEC20, 0xD, (0, 0, DIR_NAME, 0, 1, 0x821500), True))
    for fn, phase, args, nonneg in routines:
        for beat in ('o7', 'a4'):
            for v0 in (0, 1, NEG):
                out.append(('cmd', beat, {'fn': fn, 'args': args, 'b': helpers(phase=0),
                                          'card': {'issue': {0: v0}} if v0 else {}}))
            for last in (0, 3, 0x640, NEG, 0xFFFFFFFC):
                out.append(('cmd', beat, {'fn': fn, 'args': args, 'b': helpers(phase=phase, last=last)}))
            for other in (7, NEG, phase + 1):
                out.append(('cmd', beat, {'fn': fn, 'args': args, 'b': helpers(phase=other)}))
    return out


def file_items():
    out = []
    args = (MC + 0x64, 0, 0, DIR_NAME, BLOCK, 0x640)
    for beat in ('o7', 'a4'):
        for st in (0, 1, 2, 3, NEG):
            for err in (0, 1):
                for phase, last, count in ((0, 0, 0), (2, 5, 0), (2, NEG, 0), (6, 0x640, 0), (6, 0x10, 0),
                                           (6, NEG, 0), (3, 0, 0x640), (3, 0, 0x63F), (3, 1, 0x640), (9, 0, 0)):
                    for v0 in ((0, 1) if phase == 0 else (0,)):
                        out.append(('file', beat, {'fn': 0x1FE6B0, 'args': args,
                                                   'b': [(MC + 0x64, st, 4)] + helpers(err, phase, last, count, 4),
                                                   'card': {'issue': {0: v0}} if v0 else {}}))
    return out


def block_items():
    out = []
    for beat in ('o7', 'a4'):
        for flag in (0, 1, 0xFF):
            for fill in (None, 0x5A, 0xFF):
                b = [(BLOCK + 0xF, flag, 1)]
                if fill is not None:
                    b += [(BLOCK + i, (fill * (i + 7)) & 0xFF, 1)
                          for i in sorted(set(range(0, 0x640, 37)) | {0xE, 0x10, 0x63E, 0x63F}) if i != 0xF]
                out.append(('block', beat, {'fn': 0x2284F0, 'args': (BLOCK,), 'b': b}))
        out.append(('block', beat, {'fn': 0x2284F0, 'args': (0x1A00000,)}))
        out.append(('block', beat, {'fn': 0x1AF220}))
        out.append(('block', beat, {'fn': 0x1AF220, 'b': [(BLOCK, 3, 1), (BLOCK + 1, 2, 1), (BLOCK + 0x33, 9, 1)]}))
    return out


# The header's sources made distinct (bytes 0..5, byte 0xA, word 0x50), so
# that every header store is visible.
HEAD = [(BLOCK + i, 0x11 * (i + 1), 1) for i in range(6)] + [(BLOCK + 0xA, 0x5C, 1), (BLOCK + 0x50, 0x12345678, 4)]


def ok_close(count):
    """The helpers' state in which 001FE6B0's Close completes."""
    return helpers(0, 3, 0, count, 4)


def writer_items():
    out = []
    for beat in ('o7', 'a4'):
        # 00228530: GetDir issued, refused, 0 / 1 / 2 entries, an error
        for phase, last in ((0, 0), (0xD, 0), (0xD, 1), (0xD, 2), (0xD, NEG), (5, 0)):
            out.append(('writer', beat, {'fn': 0x228530, 'args': (MC,), 'b': rec(port=1) + helpers(0, phase, last)}))
        # 002285A0 steps 0..4 and 5
        for port in (0, 1):
            out.append(('writer', beat, {'fn': 0x2285A0, 'args': (MC,), 'b': rec(port, step=0) + helpers()}))
            for last in (0, NEG):
                out.append(('writer', beat, {'fn': 0x2285A0, 'args': (MC,),
                                             'b': rec(port, step=0) + helpers(0, 0xB, last)}))
        for step, size in ((1, 0x48), (2, 0x115B0), (3, 0x3C4)):
            for count in (size, size - 1):
                out.append(('writer', beat, {'fn': 0x2285A0, 'args': (MC,),
                                             'b': rec(0, step=step, phase=2) + ok_close(count)}))
            out.append(('writer', beat, {'fn': 0x2285A0, 'args': (MC,), 'b': rec(0, step=step, phase=0) + helpers()}))
        for chosen in (0, 2, 4):
            for k in (0, 2, 4):
                for count in (0x640, 0x63F):
                    out.append(('writer', beat, {'fn': 0x2285A0, 'args': (MC,),
                                                 'b': rec(1, chosen, step=4, phase=2, k=k) + ok_close(count) + HEAD}))
                out.append(('writer', beat, {'fn': 0x2285A0, 'args': (MC,),
                                             'b': rec(1, chosen, step=4, phase=1, k=k) + helpers(0, 0, 0, 0, 4)}))
                out.append(('writer', beat, {'fn': 0x2285A0, 'args': (MC,),
                                             'b': rec(1, chosen, step=4, phase=2, k=k) + helpers(1, 9, 0, 0, 4)}))
        out.append(('writer', beat, {'fn': 0x2285A0, 'args': (MC,), 'b': rec(0, step=5)}))
        # 002288A0
        for chosen in (0, 3, 4):
            for count in (0x640, 0x10):
                out.append(('writer', beat, {'fn': 0x2288A0, 'args': (MC,),
                                             'b': rec(0, chosen, step=0, phase=2) + ok_close(count) + HEAD}))
            out.append(('writer', beat, {'fn': 0x2288A0, 'args': (MC,), 'b': rec(0, chosen, step=0) + helpers()}))
        for step in (1, 2):
            out.append(('writer', beat, {'fn': 0x2288A0, 'args': (MC,), 'b': rec(0, 1, step=step, phase=7)}))
        # 00229640 (the icon file)
        out.append(('writer', beat, {'fn': 0x229640, 'args': (MC,), 'b': rec(1, phase=0) + helpers()}))
        out.append(('writer', beat, {'fn': 0x229640, 'args': (MC,), 'b': rec(1, phase=2) + ok_close(0x115B0)}))
        # 00229680 and 00228A90 on the Sync result and every outcome
        for fn in (0x229680, 0x228A90):
            for pend in (0, 6):
                for busy in (0, 1):
                    if busy and not pend:
                        continue
                    for state in ((0, 1, 2, 3, 4) if fn == 0x228A90 else (0,)):
                        for b, card_extra in ((ok_close(0x640), {}), (ok_close(0x10), {}), (helpers(), {}),
                                              (helpers(1, 9), {}), (helpers(0, 0), {'issue': {0: 1}})):
                            if fn == 0x228A90 and state != 2 and b != helpers():
                                continue
                            out.append(('writer', beat, {
                                'fn': fn, 'args': (MC,), 'b': rec(0, 2, state=state, phase=2 if b[1][1] == 3 else 0)
                                + b, 'card': {'pending': pend, 'busy': busy, **card_extra}}))
            out.append(('writer', beat, {'fn': fn, 'args': (MC,), 'b': rec(0, 2, state=1) + helpers(1, 9),
                                         'card': {'pending': 6}}))
    return out


def save_items():
    """Whole saves, one 00228A90 call per frame from its first call."""
    out = []
    for beat in ('a4', 'o7'):
        for chosen in range(5):
            for port in (0, 1):
                for exists in (False, True):
                    if port and chosen not in (0, 4):
                        continue
                    out.append(('save', beat, {'chosen': chosen, 'port': port, 'exists': exists, 'card': {}}))
        out.append(('save', beat, {'chosen': 1, 'port': 0, 'exists': False, 'card': {'busy': 2}}))
        out.append(('save', beat, {'chosen': 3, 'port': 1, 'exists': True, 'card': {'busy': 1}}))
        # a refused issue or a failing result at every command of a new directory's save (26 commands)
        for k in range(26):
            out.append(('save', beat, {'chosen': 2, 'port': 0, 'exists': False, 'card': {'result': {k: -4}}}))
            if k % 3 == 0:
                out.append(('save', beat, {'chosen': 2, 'port': 0, 'exists': False, 'card': {'issue': {k: 1}}}))
        out.append(('save', beat, {'chosen': 2, 'port': 0, 'exists': False, 'card': {'short': {9: 0x100}}}))
        out.append(('save', beat, {'chosen': 0, 'port': 0, 'exists': True, 'card': {'short': {2: 0x63F}}}))
        out.append(('save', beat, {'chosen': 1, 'port': 0, 'blank': True, 'card': {}}))
        out.append(('save', beat, {'chosen': 4, 'port': 1, 'blank': True, 'card': {'result': {1: -4}}}))
    return out


SAVE_FRAMES = 60


def image_word(beat, at):
    return struct.unpack_from('<I', R.IMAGES[beat][0], at)[0]


def check_card(item, card, start_ram):
    """docs/SAVE_GAME.md section 2: the layout of a completed save, read back
    from the card the original wrote (the oracle's model)."""
    group, beat, knobs = item
    img = R.IMAGES[beat][0]          # its names and icon.sys equal the ELF's (check_code)
    dname = cstr(img, DIR_NAME)
    names = [cstr(img, SLOT_NAMES + 0x40 * n) for n in range(5)]
    # 002284F0 runs on every frame of the slot's Open / Write / Close, so the
    # written block has +0xF = 1 and the check byte from a run that summed it:
    # all 0x640 bytes sum to zero.
    live = bytearray(start_ram[BLOCK:BLOCK + 0x640])
    live[0xF] = 1
    live[0x63F] = (0x100 - sum(live[:0x63F])) & 0xFF
    chosen = knobs['chosen']
    if knobs.get('exists'):
        assert card.order[0] == ('getdir', dname), (item, card.order[:2])
        assert set(card.files) == {names[chosen]}, (item, sorted(card.files))
        assert card.files[names[chosen]] == bytes(live), (item, 'the chosen slot is the live block + check byte')
        return
    header, iconsys, icon = cstr(img, HEADER_NAME), cstr(img, ICONSYS_NAME), cstr(img, ICON_NAME)
    want_order = [header, icon, iconsys] + names
    opened = [o[1] for o in card.order if o[0] == 'open']
    assert card.order[:2] == [('getdir', dname), ('mkdir', dname)], (item, card.order[:2])
    assert opened == want_order, (item, opened)
    assert all(o[2] == 0x203 for o in card.order if o[0] == 'open'), item
    assert all(n.startswith(dname + '/') for n in opened), item
    h = card.files[header]
    assert len(h) == 0x48 and h[0x20:0x24] == struct.pack('<I', 0x40000000), item
    assert h[:h.index(b'\0')] == cstr(img, TITLE).encode(), item
    assert card.files[iconsys] == bytes(img[ICONSYS:ICONSYS + 0x3C4]), item
    c = image_word(beat, ICON_PTR)
    first, off, size = image_word(beat, c), image_word(beat, c + 0x10), image_word(beat, c + 0x18)
    at = c + first + off
    assert card.files[icon] == bytes(start_ram[at:at + size]), item
    assert card.files[names[chosen]] == bytes(live), (item, 'the chosen slot is the live block + check byte')
    others = {card.files[names[n]] for n in range(5) if n != chosen}
    assert len(others) == 1, (item, 'the other four slots are one block')
    blank = others.pop()
    assert len(blank) == 0x640 and blank != bytes(live), item


def save_case(item):
    group, beat, knobs = item
    global CARD
    cfg = dict(knobs['card'])
    if knobs.get('exists'):
        cfg['dirs'] = [cstr(R.IMAGES[beat][0], DIR_NAME)]
    CARD = cfg
    blank = knobs.get('blank')
    b = rec(knobs['port'], knobs['chosen'], state=0) + helpers() + [(MC + 0x50, 0, 4), (MC + 0x54, 0, 4)]
    b += [(MC + 0x6C + n, 0, 1) for n in range(5)] + [(MC + 0x74 + i, 0, 1) for i in range(60)]
    ram, spad = make(beat, {'b': b})
    start_ram = bytes(ram) if not blank else None
    fn = 0x229680 if blank else 0x228A90
    busy = cfg.get('busy', 0)
    steps = [('call', fn, (MC,))] * ((12 if blank else SAVE_FRAMES) * (1 + busy))
    failing = cfg.get('result') or cfg.get('issue') or cfg.get('short')
    done = None if failing or blank else (lambda ee: ee.load(MC + 3, 1) == 3)
    r = run_both(item, bytes(spad), steps, check=done)
    card = MODELS['oracle']
    assert card.files == MODELS['native'].files and card.order == MODELS['native'].order, (item, 'cards differ')
    if blank:
        names = [cstr(R.IMAGES[beat][0], SLOT_NAMES + 0x40 * n) for n in range(5)]
        if not failing:
            assert set(card.files) == {names[knobs['chosen']]}, (item, sorted(card.files))
            assert len(card.files[names[knobs['chosen']]]) == 0x640, item
    elif not failing:
        check_card(item, card, start_ram)
    return r


GROUPS = None


def case_groups():
    return [('cmd', cmd_items(), one_case), ('file', file_items(), one_case), ('block', block_items(), one_case),
            ('writer', writer_items(), one_case), ('save', save_items(), save_case)]


def pmap(fn, items):
    items = list(items)
    if POOL is None or len(items) < 2:
        return [fn(x) for x in items]
    size = max(1, -(-len(items) // (3 * JOBS)))
    return list(POOL.imap(fn, items, chunksize=size))


# 001FE6B0 returns only -1, 0 or 1, and 0 only with the error flag
# D_00275C58 clear (its last test): the callers' fourth arm after their 1 /
# 0 / -1 tests (002285A0, 002288A0, 00229680) and 00229680's flag test
# after a 0 never take the other way.
UNREACHED = {(0x22874C, False), (0x22891C, False), (0x229714, False), (0x229740, False)}


def setup():
    global ELF, NATIVE
    ELF = read_elf()
    NATIVE = build_native()
    install()
    R.TRACKER = R.build_tracker()
    load_images()
    check_code()
    got = callee_set()
    missing = sorted(hex(a) for a in got if a not in WSPEC and a not in U.INLINE)
    assert not missing, ('callees without a spec', missing)


# The default run: the greedy branch-outcome cover of the full sweep (the
# `cover` command prints it; item indices into each group's list, which are
# deterministic) plus the whole saves 0 / 1 (a new directory and an existing
# one, checked against the layout), the data cases that make every store
# visible (block 1, 4: 002284F0 over a filled block; 10: 001AF220; writer
# 164, 188: the slot header from distinct sources).
QUICK = {
    'block': [0, 1, 4, 10],
    'cmd': [1, 8, 23, 30, 45, 48, 52, 67, 70, 74, 89, 94, 96],
    'save': [0, 1, 15, 43, 54],
    'writer': [29, 57, 59, 68, 71, 81, 98, 164, 188],
}


def sample(items, name):
    if RM.FULL or name not in QUICK:
        return items
    return [items[i] for i in QUICK[name]]


def cover():
    """Run every case, then the greedy outcome cover per group (printed as QUICK)."""
    global POOL
    setup()
    if JOBS > 1 and 'fork' in multiprocessing.get_all_start_methods():
        POOL = multiprocessing.get_context('fork').Pool(JOBS)
    per = []
    try:
        for name, items, fn in case_groups():
            for i, r in enumerate(pmap(fn, items)):
                if r not in ('unmeasured', 'skipped'):
                    per.append((name, i, frozenset(o for o in r[0] if o[0] in lane_pcs())))
    finally:
        if POOL is not None:
            R.close_pool(POOL)
            POOL = None
    left = set().union(*(o for _, _, o in per))
    chosen = {}
    while left:
        name, i, o = max(per, key=lambda t: len(t[2] & left))
        chosen.setdefault(name, []).append(i)
        left -= o
    for must in ('save',):          # the decode: a new directory and an existing one, every run
        chosen.setdefault(must, [])
        chosen[must] = sorted(set(chosen[must]) | {0, 1})
    print('QUICK = {')
    for name in sorted(chosen):
        print('    %r: %r,' % (name, sorted(chosen[name])))
    print('}')


def main():
    global POOL
    t0 = time.time()
    setup()
    if JOBS > 1 and 'fork' in multiprocessing.get_all_start_methods():
        POOL = multiprocessing.get_context('fork').Pool(JOBS)
    outcomes, cases, calls, entries = set(), 0, 0, 0
    counts = []
    try:
        for name, items, fn in case_groups():
            chosen = sample(items, name)
            for r in pmap(fn, chosen):
                cases += 1
                if r in ('unmeasured', 'skipped'):
                    continue
                outcomes.update(r[0])
                calls += r[1]
                entries += r[2]
            counts.append(RM.part(len(chosen), len(items), name + ' cases'))
    finally:
        if POOL is not None:
            R.close_pool(POOL)
            POOL = None
    branches = all_branches()
    need = {(pc, t) for pc in branches for t in (True, False)}
    got = {o for o in outcomes if o[0] in lane_pcs()}
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
    print('save reference: PASS')


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
    if len(sys.argv) > 1 and sys.argv[1] == 'debug':
        debug(sys.argv[2:])
    elif len(sys.argv) > 1 and sys.argv[1] == 'cover':
        cover()
    else:
        main()
