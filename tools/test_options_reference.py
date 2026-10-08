#!/usr/bin/env python3
"""Execute the ORIGINAL options-screen and memory-card-screen routines and
compare the native em_options_* translations byte for byte.

docs/OPTIONS.md. The user's pinned ELF and the capture lane OPTIONS's and
DAMAGE's recorded images (../Extermination/build/c10/options/<beat>/ and
../Extermination/build/c10/damage/dmg_05_load_screen/, eeMemory.bin and
scratchpad.bin) supply every instruction and every table; none are
embedded here.

Method: the AREA01 UI lane's (tools/test_area01_ui_reference.py; its
interpreter, dirty-page tracker, entry check and run_both, pointed at this
lane's routines). The oracle runs the original routine over a captured
image with every callee outside the lane run as ORIGINAL code too, nested,
its entry logged (address, stack pointer, the integer argument registers it
takes as 64-bit images, the float argument registers). The native module
runs over a second copy; each callee it reaches runs the same original code
in a second interpreter sharing its memory. At every callee entry all 32 MiB
of RAM and the scratchpad must equal the oracle's at the same call; at the
end the results, the call log, RAM and scratchpad must be identical.

Scripted boundaries (both sides; entry checked and logged, not run): the
callees that reach the IOP, a DMA channel or the pad library (the memory
card SDK's GetInfo 00114988 and Sync 00114848, 001FBC50, 001FABB0, 001B61C0,
00200970's VIF1 packet, 001FF080's task slot) and the two card screens past
the first level's recordings (002267A0, 00227300). A case scripts their v0
(and the words a Sync completion stores through its result pointer) in
call order. Quick mode also scripts 0020A7A0 (the moving background,
verified by test_status_background_reference).

Cases are designed inputs over the captured images: every state and
sub-state of 0022A650 and its row screens with the pad edges and repeats
that act on them, the list over every row and setting, the text with and
without its token, the card screen's states, the poll results the SDK
returns and the slot choice with one, two or no cards. Every conditional
branch of the lane must be taken both ways (EM_TEST_FULL=1) except the ones
UNREACHED lists.

EM_TEST_FULL=1 runs every case; the default a covering sample. Up to four
worker processes (EM_TEST_JOBS=1: serial). Commands: `debug <group>[:i,..]`.
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
os.environ.setdefault('EM_LANE', 'options')
import reference_mode as RM  # noqa: E402
import test_area01_render_reference as R  # noqa: E402
import test_area01_ui_reference as U  # noqa: E402
from test_player_slide_reference import EE, read_elf  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
OUT = ROOT / 'build' / 'options' / 'reference'
R.OUT = U.OUT = OUT
JOBS = R.JOBS

CAP = ROOT.parent / 'Extermination' / 'build' / 'c10'
IMAGES = {
    'o0': CAP / 'options' / 'opt_00_browse_close',
    'o3': CAP / 'options' / 'opt_03_screen_position',
    'o7': CAP / 'options' / 'opt_07_load_cancel',
    'd5': CAP / 'damage' / 'dmg_05_load_screen',
}
BEATS = list(IMAGES)

LANE = {
    0x22A650: 0x848, 0x22A590: 0xBC, 0x22AEA0: 0x578, 0x201720: 0x52C, 0x201C50: 0x318, 0x201F70: 0xC2C,
    0x202BA0: 0x168, 0x202D10: 0x5AC, 0x22B420: 0x2D8, 0x1AF6F0: 0x14, 0x1AF1C0: 0x5C, 0x1AF150: 0x64,
    0x1AF470: 0x148, 0x1FCBD0: 0x260, 0x1FCE30: 0xE0, 0x225AC0: 0x200, 0x225720: 0x2DC, 0x225D20: 0x2E8,
    0x226070: 0x730, 0x225A20: 0x9C, 0x225CF0: 0x30, 0x2256E0: 0x20, 0x225700: 0x20, 0x226010: 0x58,
    0x1FE9A0: 0x38, 0x1FECB0: 0x84, 0x1FE920: 0x80, 0x1FE8D0: 0x40,
}
TRPC = frozenset(pc for a, n in LANE.items() for pc in range(a, a + n, 4))

# callee -> (integer argument registers, float argument registers, result used)
WSPEC = dict(U.WSPEC)
WSPEC.update({
    0x114848: (3, 0, 'v0'), 0x114988: (5, 0, 'v0'), 0x121A28: (3, 0, None), 0x122EF0: (2, 0, None),
    0x123280: (2, 0, 'v0'), 0x1232E0: (1, 0, 'v0'), 0x123418: (3, 0, None), 0x1AEDE0: (2, 0, None),
    0x1AEE10: (2, 0, None), 0x1B61C0: (4, 0, None), 0x1CC170: (1, 0, 'v0'), 0x1CC1E0: (7, 0, None),
    0x1D2830: (2, 0, None), 0x1FABB0: (0, 0, None), 0x1FBC50: (0, 0, None), 0x1FC770: (4, 0, None),
    0x1FE480: (2, 0, 'v0'), 0x1FF080: (2, 0, None), 0x200970: (1, 0, None), 0x207F80: (6, 0, None),
    0x2267A0: (1, 0, 'v0'), 0x227300: (1, 0, 'v0'),
})

# Scripted boundaries (see the docstring). 0020A7A0 only in quick mode.
STUBS = {0x114848, 0x114988, 0x1FBC50, 0x1FABB0, 0x1B61C0, 0x200970, 0x1FF080, 0x2267A0, 0x227300}
if not RM.FULL:
    STUBS |= {0x20A7A0}
SCRIPT = {}          # address -> list of (v0, {address: (value, size)}) consumed in call order

P, U8, U32, I32, U64 = C.POINTER, C.c_uint8, C.c_uint32, C.c_int32, C.c_uint64
ENTRIES = {  # native entry -> (argument ctypes, has a v0 result)
    0x22A650: ([], True), 0x22A590: ([], True), 0x22AEA0: ([U32, I32, I32], False),
    0x201720: ([U32, I32, U32, U32], True), 0x201C50: ([U32, I32, U32, U32], True),
    0x201F70: ([U32], True), 0x202BA0: ([], True), 0x202D10: ([U32], True),
    0x22B420: ([U32, I32, U32, U32], True), 0x1AF6F0: ([], False), 0x1AF1C0: ([], False),
    0x1AF150: ([], False), 0x1AF470: ([U32], False), 0x1FCBD0: ([U64] * 5, False),
    0x1FCE30: ([U64] * 5, False), 0x225AC0: ([U32], True), 0x225720: ([U32], False),
    0x225D20: ([U32], True), 0x226070: ([U32], True), 0x225A20: ([], False),
    0x225CF0: ([U32, U32, U32], False), 0x2256E0: ([], False), 0x225700: ([], False),
    0x226010: ([U32], True), 0x1FE9A0: ([U64] * 3, True), 0x1FECB0: ([U64] * 5, True),
    0x1FE920: ([U64] * 2, True), 0x1FE8D0: ([], False),
}
SOURCES = ['src/game/em_options_original.c', 'src/game/em_startup_load_gaps.c']
CFLAGS = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-ffp-contract=off', '-shared', '-fPIC',
          '-Isrc']
ELF = NATIVE = None
POOL = None

W_SET = 0x810118                   # D_00810118
MC = 0x810040                      # D_00810040
SLOT = 0x28A7B0                    # *0x70003B6C in every image


def build_native():
    import hashlib
    import subprocess
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('options' + ('.dylib' if sys.platform == 'darwin' else '.so'))
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
        fn = getattr(native, 'em_options_%08X' % address)
        fn.argtypes = [P(U.UiState)] + args + ([P(U32)] if result else [])
        fn.restype = C.c_int
    return native


# ---- scripted boundaries --------------------------------------------------

def script_next(address, counters):
    k = counters.get(address, 0)
    counters[address] = k + 1
    queue = SCRIPT.get(address, ())
    return queue[k] if k < len(queue) else (0, {})


def stub_hook(e, address):
    spec = U.WSPEC[address]
    entry = U.entry_of(e, address, spec)
    e.entries.append((len(e.journal), entry))
    v0, writes = script_next(address, e.script_counters)
    for at, (value, size) in writes.items():
        e.save(at, value, size)
    e.calls.append(entry + (U.result_of(spec, v0, 0),))
    e.exits.append((len(e.journal), v0 & MASK64, 0))
    e.r[2], e.f[0] = v0 & MASK64, 0


BASE_ORACLE_HOOKS = U.oracle_hooks


def oracle_hooks(ee):
    BASE_ORACLE_HOOKS(ee)
    ee.script_counters = {}
    for a in STUBS:
        if a in ee.hooks:
            ee.hooks[a] = lambda e, a=a: stub_hook(e, a)


class Native(U.Native):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.script_counters = {}

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
        value, writes = script_next(target, self.script_counters)
        for at, (val, size) in writes.items():
            self.ee.save(at, val, size)
        self.log.append(entry + (U.result_of(spec, value, 0),))
        v0[0] = value & MASK64
        f0[0] = 0
        return 0

    def call(self, address, args):
        types, result = ENTRIES[address]
        fn = getattr(U.NATIVE, 'em_options_%08X' % address)
        out = U32(0)
        rc = fn(C.byref(self.state), *args, *([C.byref(out)] if result else []))
        return rc, (out.value if result else None)


def oracle_regs(address, args):
    return list(args)


def run_both(where, spad, steps):
    U.WSPEC = {a: spec for a, spec in WSPEC.items() if a not in LANE}
    return U.run_both(where, spad, steps)


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


def check_code():
    base = EE(ELF)
    for name in BEATS:
        ram = R.IMAGES[name][0]
        for start in sorted(set(LANE) | set(WSPEC)):
            size = LANE.get(start, 0x40)
            assert ram[start:start + size] == bytes(base.mem[start:start + size]), (name, hex(start))


def lane_pcs():
    return frozenset(pc for a, n in LANE.items() for pc in range(a, a + n, 4))


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


# ---- case helpers -----------------------------------------------------------

put = U.put


def case(beat):
    return R.case_ram(beat), bytearray(R.IMAGES[beat][1])


def sput(spad, at, value, size=4):
    spad[at - 0x70000000:at - 0x70000000 + size] = (value & ((1 << 8 * size) - 1)).to_bytes(size, 'little')


def apply(ram, spad, knobs):
    """'pads' (e70, e74, e78), 'b' [(address, value, size)], 'spad' [(address, value, size)],
    'slot' {offset: (value, size)} on the task record, 'set' bytes of D_00810118."""
    if 'pads' in knobs:
        e70, e74, e78 = knobs['pads']
        put(ram, 0x810E70, e70, 2)
        put(ram, 0x810E74, e74, 2)
        put(ram, 0x810E78, e78, 2)
    for i, v in enumerate(knobs.get('set', ())):
        put(ram, W_SET + i, v, 1)
    for at, v, size in knobs.get('b', ()):
        put(ram, at, v, size)
    for off, (v, size) in knobs.get('slot', {}).items():
        put(ram, SLOT + off, v, size)
    for at, v, size in knobs.get('spad', ()):
        sput(spad, at, v, size)


def make(item):
    group, beat, knobs = item[:3]
    ram, spad = case(beat)
    apply(ram, spad, knobs)
    return ram, spad


def screen_case(item):
    group, beat, knobs = item
    ram, spad = make(item)
    global SCRIPT
    SCRIPT = knobs.get('script', {})
    steps = [('call', 0x22A650, ())] * knobs.get('frames', 1)
    return run_both(item, bytes(spad), steps)


def row_case(item):
    group, beat, knobs = item
    ram, spad = make(item)
    global SCRIPT
    SCRIPT = knobs.get('script', {})
    a = knobs['fn']
    args = knobs.get('args')
    if args is None:
        args = {0x201720: (W_SET, 0x9E, 0x2672C0, 0x2672E0), 0x201C50: (W_SET, 0xB6, 0x2672C0, 0x2672E0),
                0x22B420: (W_SET, 0x86, 0x2672C0, 0x2672E0), 0x201F70: (W_SET,), 0x202D10: (W_SET,),
                0x202BA0: (), 0x22A590: (), 0x1AF6F0: (), 0x1AF1C0: (), 0x1AF150: ()}[a]
    steps = [('call', a, tuple(args))] * knobs.get('frames', 1)
    return run_both(item, bytes(spad), steps)


def card_case(item):
    group, beat, knobs = item
    ram, spad = make(item)
    global SCRIPT
    SCRIPT = knobs.get('script', {})
    a = knobs['fn']
    args = knobs.get('args', (MC,) if a in (0x225720, 0x225D20, 0x226070, 0x226010) else ())
    steps = [('call', a, tuple(args))] * knobs.get('frames', 1)
    return run_both(item, bytes(spad), steps)


# ---- items ------------------------------------------------------------------

UP, DOWN, LEFT, RIGHT = 0x1000, 0x4000, 0x8000, 0x2000
CROSS, CIRCLE, TRIANGLE, SELECT = 0x40, 0x20, 0x10, 0x100


def slot(state, sub=0, cursor=0, saved=0, timer=0):
    return {0xC: (state, 1), 0xD: (sub, 1), 0x1C: (cursor, 2), 0x13: (saved, 1), 0x1E: (timer, 2)}


def screen_items():
    out = []
    for beat in ('o0', 'o3'):
        for e6a in (7, 0):
            for cursor in range(9):
                for pads in ((0, 0, 0), (0, 0, DOWN), (0, 0, UP), (0, CROSS, 0), (0, CIRCLE, 0), (0, TRIANGLE, 0),
                             (0, SELECT, 0)):
                    out.append(('screen', beat, {'slot': slot(1, 0, cursor), 'pads': pads,
                                                 'b': [(0x810E6A, e6a, 2)]}))
        for cursor in (0, 8):
            out.append(('screen', beat, {'slot': slot(0, 0, cursor), 'pads': (0, 0, DOWN)}))
        out.append(('screen', beat, {'slot': slot(1, 0, 0xFFFF), 'pads': (0, 0, DOWN)}))
        out.append(('screen', beat, {'slot': slot(1, 0, 0x100), 'pads': (0, 0, UP)}))
        for st in (2, 11, 12, 13):
            out.append(('screen', beat, {'slot': slot(st)}))
        for v in (0, 1, 2, 3):
            out.append(('screen', beat, {'slot': slot(4, 1, 8), 'pads': (0, (0, CROSS, CIRCLE, TRIANGLE)[v], 0)}))
            out.append(('screen', beat, {'slot': slot(4, 1, 8, 1), 'pads': (0, CROSS, 0)}))
        for st, cursor in ((5, 1), (5, 2), (6, 7)):
            for pads in ((0, 0, 0), (0, CROSS, 0), (0, CIRCLE, 0), (0, TRIANGLE, 0)):
                out.append(('screen', beat, {'slot': slot(st, 1, cursor), 'pads': pads}))
        for st in (7, 8, 9):
            for pads in ((0, 0, 0), (0, CROSS, 0), (0, TRIANGLE, 0)):
                out.append(('screen', beat, {'slot': slot(st, 1, 3), 'pads': pads}))
        for sub, busy, phase in ((0, 0, 0), (1, 0, 0), (1, 0, 1), (2, 1, 0), (2, 0, 0)):
            out.append(('screen', beat, {'slot': {**slot(10, sub), 0x13: (7, 1)},
                                         'b': [(0x275BD8, busy, 1), (0x282157, phase, 1)]}))
        for m0, res in ((3, 0), (3, 1), (3, 2)):
            out.append(('screen', beat, {'slot': slot(3), 'b': [(MC, m0, 1), (MC + 0x16, res, 1)]}))
    # Crafted action tables D_002672E0 (the image's is 7, 0, 1, 2, 3, 4, 9, 5,
    # 8): the vibration action at either end (the second step's wraps), twice
    # in a row (Cross on it without the pad byte 7) and an action past the
    # jump table (10).
    for table in ((7, 1, 2, 3, 4, 9, 5, 8, 0), (0, 7, 1, 2, 3, 4, 9, 5, 8), (7, 0, 0, 1, 2, 3, 4, 9, 5),
                  (7, 10, 1, 2, 3, 4, 9, 5, 8)):
        for cursor in (0, 1, 7, 8):
            for pads in ((0, 0, DOWN), (0, 0, UP), (0, CROSS, 0), (0, 0, 0)):
                for e6a in (7, 0):
                    out.append(('screen', 'o0', {'slot': slot(1, 0, cursor), 'pads': pads,
                                                 'b': [(0x810E6A, e6a, 2)] +
                                                      [(0x2672E0 + 4 * i, v, 4) for i, v in enumerate(table)]}))
    return out


def list_items():
    out = []
    for beat in ('o0',):
        for row in (7, 0, 1, 2, 3, 4, 9, 5, 8, 6):
            for opened in (0, 1):
                for sets in ((0, 1, 0, 0, 0), (1, 0, 0, 1, 1), (2, 1, 0, 0, 0)):
                    for e6a in (7, 3):
                        out.append(('rows', beat, {'fn': 0x22AEA0, 'args': (W_SET, row, opened), 'set': sets,
                                                   'b': [(0x810E6A, e6a, 2)], 'slot': {0x13: (opened, 1)}}))
    return out


def row_items():
    out = []
    beat = 'o0'
    for fn, cursors in ((0x201720, (1, 2)), (0x201C50, (7,)), (0x22B420, (8,))):
        for cursor in cursors:
            for sub in (0, 1, 2, 3):
                for timer in (1, 5):
                    for pads in ((0, 0, 0), (0, RIGHT, 0), (0, CROSS, 0), (0, CIRCLE, 0), (0, TRIANGLE, 0),
                                 (0, RIGHT | CROSS, 0)):
                        for sets in ((0, 1, 0, 0, 0), (0, 0, 0, 1, 1)):
                            for e6a, e50 in ((7, 4), (3, 4), (7, 2)):
                                if (e6a, e50) != (7, 4) and fn != 0x201720 and timer == 5:
                                    continue
                                out.append(('rows', beat, {'fn': fn, 'slot': slot(5, sub, cursor, 1, timer),
                                                           'pads': pads, 'set': sets,
                                                           'b': [(0x810E6A, e6a, 2), (0x810E50, e50, 1)]}))
    for sub in (0, 1, 2):
        for e70 in (0, LEFT, RIGHT, UP, DOWN, LEFT | UP):
            for e78 in (0, UP, DOWN, LEFT, RIGHT, UP | RIGHT, DOWN | LEFT):
                for off in ((0, 0), (0x14, -0x14), (-0x14, 0x14), (3, -2)):
                    for e74 in (0, CROSS, CIRCLE, TRIANGLE):
                        if e74 and (e70 or e78):
                            continue
                        out.append(('rows', beat, {'fn': 0x201F70, 'slot': slot(7, sub), 'pads': (e70, e74, e78),
                                                   'spad': [(0x70003B94, off[0], 2), (0x70003B96, off[1], 2)],
                                                   'set': (0, 1, 0, 0, 0, 0, 0, 0, 5, 0, 0xFE, 0xFF)}))
    for e74 in (0, CROSS, CIRCLE, TRIANGLE, 0x70):
        out.append(('rows', beat, {'fn': 0x202BA0, 'pads': (0, e74, 0)}))
    for sub in (0, 1):
        for typ in (0, 1, 2):     # a type above 2 is never stored (D_00264F98 has three rows)
            for e74 in (0, RIGHT, LEFT, CROSS, CIRCLE, TRIANGLE):
                out.append(('rows', beat, {'fn': 0x202D10, 'slot': slot(9, sub, 5, 1), 'pads': (0, e74, 0),
                                           'set': (typ,)}))
    for sub, busy, phase in ((0, 0, 0), (1, 0, 0), (1, 0, 0xFF), (2, 0, 0), (2, 1, 0), (3, 0, 0)):
        out.append(('rows', beat, {'fn': 0x22A590, 'slot': slot(10, sub),
                                   'b': [(0x275BD8, busy, 1), (0x282157, phase, 1)]}))
    for typ in (0, 1, 2, 3, 0x102):
        out.append(('rows', beat, {'fn': 0x1AF470, 'args': (typ,)}))
    out.append(('rows', beat, {'fn': 0x1AF6F0}))
    for sets in ((1, 0, 0, 0, 1, 0, 0, 0, 0xFE, 0xFF, 3, 0), (2, 1, 0, 0, 0, 0, 0, 0, 0x14, 0, 0xEC, 0xFF)):
        out.append(('rows', beat, {'fn': 0x1AF1C0, 'set': sets}))
    for b in ((2, 0, 1, 0xFFF0, 7), (0, 1, 0, 3, 0x14), (5, 1, 1, 0, 0)):
        out.append(('rows', beat, {'fn': 0x1AF150, 'b': [(0x810708, b[0], 1), (0x810709, b[1], 1),
                                                         (0x81070C, b[2], 1), (0x810754, b[3], 2),
                                                         (0x810756, b[4], 2)]}))
    return out


def text_items():
    """001FCBD0 / 001FCE30 over the help container's groups 7 and 8 (every
    line of group 8 the options draw), and crafted lines in a free area of
    RAM for the token splice (the hex digit classes and the refusals)."""
    out = []
    beat = 'o0'
    for line in range(0, 0x29):
        out.append(('text', beat, {'fn': 0x1FCBD0, 'args': (0x6F, 0x47, 8, line, 0xFFFFFFFF80808080)}))
    for line in (0, 0x13, 0x14, 0x16):
        out.append(('text', beat, {'fn': 0x1FCE30, 'args': (0xD0, 0x25, 8, line, 0xFFFFFFFF80808080)}))
    for line in (2, 3, 4, 9, 0xA, 0xB, 0x21):
        out.append(('text', beat, {'fn': 0x1FCBD0, 'args': (0x3C, 0x9B, 7, line, 0x70808080)}))
        out.append(('text', beat, {'fn': 0x1FCE30, 'args': (0x3C, 0x9B, 7, line, 0x70808080)}))
    return out


CRAFT = 0x1A00000                  # zero in every image: the crafted container lives here


def craft_items():
    """001FCBD0's token splice over crafted group-8 lines: a container at
    CRAFT (D_0028A498 pointed at it) whose group 8 line 0 is the body
    (001FE480's layout: bank +0 the directory offset, the directory's +8
    the table offset, the table's +0 its base, +0x10 the entries). The
    token character is the first byte of D_0026EC50 in the image."""
    out = []
    for body in (b'AB%c7A12CD', b'AB%c79ZZ', b'AB%c80fg', b'AB%c6112', b'AB%c9112', b'AB%c8F', b'ABCDEF',
                 b'%c7F', b'AB%c7', b'AB%c7fz', b'AB%c8Zz', b'AB%c7 x'):
        out.append(('craft', 'o0', {'fn': 0x1FCBD0, 'args': (0x10, 0x20, 8, 0, 0xFFFFFFFF80808080),
                                    'body': body}))
    return out


def craft_case(item):
    group, beat, knobs = item
    ram, spad = make(item)
    token = R.IMAGES[beat][0][0x26EC50]
    body = knobs['body'] % token if b'%c' in knobs['body'] else knobs['body']
    put(ram, 0x28A498, CRAFT)
    put(ram, CRAFT, 0x100)
    put(ram, CRAFT + 0x10 + 16 * 8, 0)
    bank = CRAFT + 0x100
    put(ram, bank, 0x20)
    put(ram, bank + 8, 0x20)
    tab = bank + 0x40
    put(ram, tab, 0x40)
    put(ram, tab + 0x10, 0)
    for i, c in enumerate(body + b'\0'):
        put(ram, tab + 0x40 + i, c, 1)
    global SCRIPT
    SCRIPT = {}
    return run_both(item, bytes(spad), [('call', knobs['fn'], knobs['args'])])


def card_items():
    out = []
    for beat in ('o7', 'd5'):
        for mode in (0, 1, 2):
            for m1, busy, phase in ((0, 0, 0), (1, 0, 0), (1, 0, 1), (2, 0, 0), (3, 0, 0), (3, 1, 0), (4, 0, 0)):
                out.append(('card', beat, {'fn': 0x225AC0, 'args': (mode,), 'b': [(MC, 0, 1), (MC + 1, m1, 1),
                                           (0x275BD8, busy, 1), (0x282157, phase, 1)]}))
        for m0 in (1, 2, 3, 4):
            for fade, b90 in ((0, 0), (2, 2), (2, 0)):
                out.append(('card', beat, {'fn': 0x225AC0, 'args': (0,),
                                           'b': [(MC, m0, 1), (MC + 0x15, 3, 1), (MC + 0x16, 1, 1),
                                                 (0x28A9A0, fade, 2)], 'spad': [(0x70003B90, b90, 1)]}))
        # 00225AC0 state 1 over the slot screen in its slot choice: Cross
        # with a card (1), Circle (the cancel, 2 then 3) and nothing (0).
        for e74, st in ((0, 1), (CROSS, 1), (CIRCLE, 1), (TRIANGLE, 1), (0, 3)):
            out.append(('card', beat, {'fn': 0x225AC0, 'args': (0,),
                                       'b': [(MC, 1, 1), (MC + 0x15, st, 1), (MC + 1, 2, 1), (MC + 0x28, 1, 4),
                                             (MC + 0x2C, 0, 4), (MC + 0x17, 0, 1), (0x275C58, 0, 4)],
                                       'pads': (0, e74, 0), 'script': {0x114848: [(1, {})]}}))
            for b93 in (0, 1):
                out.append(('card', beat, {'fn': 0x225D20,
                                           'b': [(MC + 0x15, st, 1), (MC + 1, 2, 1), (MC + 0x28, 1, 4),
                                                 (MC + 0x2C, 1, 4), (MC + 0x17, 0, 1), (0x275C58, 0, 4)],
                                           'pads': (0, e74, 0), 'spad': [(0x70003B93, b93, 1)],
                                           'script': {0x114848: [(1, {})]}}))
        for m14, m15, row in ((2, 2, 0), (1, 2, 3), (2, 0, 1)):
            out.append(('card', beat, {'fn': 0x225720, 'b': [(MC + 0x14, m14, 1), (MC + 0x15, m15, 1),
                                                             (MC + 0xA, row, 1)]}))
        out.append(('card', beat, {'fn': 0x225A20}))
        out.append(('card', beat, {'fn': 0x225CF0, 'args': (MC, 1, 7)}))
        out.append(('card', beat, {'fn': 0x225CF0, 'args': (MC, 5, 4)}))
        out.append(('card', beat, {'fn': 0x2256E0}))
        out.append(('card', beat, {'fn': 0x225700}))
        out.append(('card', beat, {'fn': 0x1FE8D0}))
        for v, res in ((1, 0), (1, 0xFFFFFFFF), (0, 0), (0xFFFFFFFF, 0)):
            out.append(('card', beat, {'fn': 0x1FE9A0, 'args': (1, MC + 0x50, MC + 0x54),
                                       'script': {0x114848: [(v, {MC + 0x54: (res, 4)} if v == 1 else {})]}}))
        for phase in (0, 1, 2, 0xFFFFFFFF):
            for last in (0, 1, 0xFFFFFFFE):
                for issue in (0, 0xFFFFFF9C):
                    out.append(('card', beat, {'fn': 0x1FECB0, 'args': (1, 0, MC + 0x58, 0, 0),
                                               'b': [(0x275C5C, phase, 4), (0x275C64, last, 4)],
                                               'script': {0x114988: [(issue, {})]}}))
        for phase in (0, 5, 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFD):
            out.append(('card', beat, {'fn': 0x1FE920, 'args': (1, 0), 'b': [(0x275C5C, phase, 4)]}))
        for v, nxt in ((0, 0), (1, 0), (1, 0xFFFFFFFE), (0xFFFFFFFF, 0xFFFFFFFE)):
            out.append(('card', beat, {'fn': 0x226010, 'b': [(MC + 0x48, 1, 4), (MC + 0x4C, 0, 4),
                                                             (0x275C5C, nxt, 4)],
                                       'script': {0x114848: [(v, {})]}}))
        # 00226070 state 0: the poll of slot 1 / 2 on every Sync and GetInfo result
        for sub in (0, 1, 2):
            for err in (0, 1):
                for sync in (0, 1, 0xFFFFFFFF):
                    for phase, last, typ in ((0, 0, 2), (1, 0, 2), (1, 0, 0), (1, 0xFFFFFFFF, 2),
                                             (1, 0xFFFFFFFE, 2), (1, 0xFFFFFFF7, 2), (1, 0xFFFFFFFD, 2),
                                             (1, 3, 2)):
                        if err and (sync, phase) != (0, 0):
                            continue
                        out.append(('card', beat, {'fn': 0x226070,
                                                   'b': [(MC + 1, 0, 1), (MC + 2, sub, 1), (0x275C58, err, 4),
                                                         (0x275C5C, phase, 4), (0x275C64, last, 4),
                                                         (MC + 0x54, last, 4), (MC + 0x58, typ, 4)],
                                                   'script': {0x114848: [(sync, {}), (sync, {})]}}))
        for v in (0, 1, 2, 3, 0xFFFFFFFF):
            out.append(('card', beat, {'fn': 0x226070, 'b': [(MC + 1, 1, 1)], 'script': {0x2267A0: [(v, {})]}}))
        for st in (3, 4, 9):
            out.append(('card', beat, {'fn': 0x226070, 'b': [(MC + 1, st, 1)]}))
        # 00226070 state 2: the slot choice
        for one, two in ((1, 1), (1, 0), (0, 1), (0, 0)):
            for sel in (0, 1):
                for e74 in (0, UP, DOWN, CROSS, CIRCLE, TRIANGLE, UP | DOWN):
                    for sync in (1, 0):
                        for rep, last, typ in ((0, 0, 2), (1, 0, 0), (2, 0xFFFFFFFE, 2), (3, 0xFFFFFFFF, 2),
                                               (4, 5, 2), (5, 0xFFFFFFFC, 2)):
                            if (e74 not in (0, CROSS) or sync == 0) and rep > 1:
                                continue
                            out.append(('card', beat, {'fn': 0x226070,
                                                       'b': [(MC + 1, 2, 1), (MC + 0x28, one, 4), (MC + 0x2C, two, 4),
                                                             (MC + 0x48, sel, 4), (MC + 0x24, rep, 4),
                                                             (MC + 0x17, 0, 1), (0x275C58, 0, 4),
                                                             (0x275C5C, 1, 4), (0x275C64, last, 4),
                                                             (MC + 0x54, last, 4), (MC + 0x58, typ, 4)],
                                                       'pads': (0, e74, 0),
                                                       'script': {0x114848: [(sync, {})]}}))
            out.append(('card', beat, {'fn': 0x226070, 'b': [(MC + 1, 2, 1), (MC + 0x28, one, 4),
                                                             (MC + 0x2C, two, 4), (0x275C58, 1, 4)]}))
        # the slot choice's re-poll of either slot over every poll outcome
        for rep in (0, 1):
            for phase in (0, 1):
                for last in (0, 0xFFFFFFFF, 0xFFFFFFFE, 5, 0xFFFFFFFC):
                    for typ in (2, 0):
                        if phase == 0 and (last or typ != 2):
                            continue
                        out.append(('card', beat, {'fn': 0x226070,
                                                   'b': [(MC + 1, 2, 1), (MC + 0x28, 1, 4), (MC + 0x2C, 1, 4),
                                                         (MC + 0x24, rep, 4), (MC + 0x17, 0, 1),
                                                         (0x275C58, 0, 4), (0x275C5C, phase, 4),
                                                         (0x275C64, last, 4), (MC + 0x54, last, 4),
                                                         (MC + 0x58, typ, 4)],
                                                   'script': {0x114848: [(1, {})], 0x114988: [(0, {})]}}))
        # 00225D20
        for st in (0, 1, 2, 3, 4):
            for busy, choice in ((0, 0), (1, 0), (1, 1), (1, 2)):
                for e74 in (0, CROSS, CIRCLE, TRIANGLE, RIGHT, LEFT):
                    for b93 in (0, 1):
                        if st != 0 and (busy or e74 or choice):
                            continue
                        for r in ((0, 1, 2, 3) if st in (1, 2) else (0,)):
                            out.append(('card', beat, {'fn': 0x225D20,
                                                       'b': [(MC + 0x15, st, 1), (MC + 0x19, busy, 1),
                                                             (MC + 9, choice, 1), (MC + 1, 2 if st == 1 else 0, 1),
                                                             (MC + 0x28, 1, 4), (MC + 0x2C, 1, 4),
                                                             (0x275C58, 0, 4)],
                                                       'pads': (0, e74 if st == 1 and r == 2 else e74, 0),
                                                       'spad': [(0x70003B93, b93, 1)],
                                                       'script': {0x227300: [(r, {})],
                                                                  0x114848: [(1, {})]}}))
    return out


def text_case(item):
    group, beat, knobs = item
    ram, spad = make(item)
    global SCRIPT
    SCRIPT = {}
    return run_both(item, bytes(spad), [('call', knobs['fn'], knobs['args'])])


GROUPS = None


def case_groups():
    return [('screen', screen_items(), screen_case), ('rows', list_items() + row_items(), row_case),
            ('text', text_items(), text_case), ('craft', craft_items(), craft_case),
            ('card', card_items(), card_case)]


def pmap(fn, items):
    items = list(items)
    if POOL is None or len(items) < 2:
        return [fn(x) for x in items]
    size = max(1, -(-len(items) // (3 * JOBS)))
    return list(POOL.imap(fn, items, chunksize=size))


# 00202D10's sub-state other than 0 / 1 (its icon table is then the caller's
# s0, refused natively as code 6): only 00202D10 itself writes that byte
# while it runs, and it only steps 0 to 1.
UNREACHED = {(0x202D40, False)}


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


# The default run: the greedy branch-outcome cover of every group (the
# `cover` command prints it from a full run; item indices into each group's
# list, which are deterministic), so a quick run takes every outcome the
# full sweep takes.
QUICK = {
    'card': [0, 1, 2, 3, 4, 5, 6, 7, 14, 21, 24, 25, 26, 30, 33, 37, 39, 44, 49, 53, 62, 73, 88, 89, 91, 94, 102, 103,
             104, 105, 107, 114, 118, 127, 128, 129, 130, 131, 132, 152, 169, 170, 171, 172, 174, 175, 176, 180, 181,
             186, 227, 245, 249, 325, 339, 417, 471, 472, 480, 481, 485, 491, 505, 509, 511, 513, 517, 523, 525,
             531, 547, 548, 549, 550, 553, 557],
    'craft': [0, 2, 8, 9],
    'rows': [4, 14, 92, 127, 129, 266, 336, 414, 600, 726, 804, 828, 856, 894, 1002, 1020, 1032, 1116, 1229, 1432,
             1530, 1626, 1627, 1633, 1634, 1638, 1640, 1666, 1670],
    'screen': [3, 10, 29, 37, 47, 55, 64, 65, 88, 120, 133, 134, 135, 136, 140, 142, 145, 148, 150, 152, 153, 154,
               155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 345, 359, 371, 385,
               450],
    'text': [46],
}


def sample(items, name):
    if RM.FULL or name not in QUICK:
        return items
    return [items[i] for i in QUICK[name]]


def cover():
    """Run every case, then the greedy outcome cover per run (printed as QUICK)."""
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
    if missing and os.environ.get('EM_OPT_SHOW_MISSING'):
        print('missing:', missing)
    print('elapsed %.1f s' % (time.time() - t0))
    print('options reference: PASS')


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
