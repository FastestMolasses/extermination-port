#!/usr/bin/env python3
"""Execute the ORIGINAL level-4 status-screen routines (lane L4UI) and
compare the native translations byte for byte, over the recorded AREA01
revisit and AREA02 captures.

docs/AREA02_UI.md. The user's pinned ELF and the level-4 route captures
(../Extermination/build/s87/route_a01r/<beat>/ and route_a02/<beat>/,
eeMemory.bin + scratchpad.bin) supply every instruction and every table;
none are embedded here.

Two parts:

  lane    the three translations of this lane (src/game/em_area02_ui.c):
          00209DF0 (the status hub's 2D layer), 00208750 (the marker glow
          packets) and 00208AB0 (the 001D66A0 forwarder).
  reuse   the port's existing verified translations of the other 15
          functions of the lane's census rows, run over the level-4 images
          with the call shapes the level-4 side beats reached:
            em_status_pages (00211970 with 002121A0 / 002125B0 / 00212B60 /
            00212F30 / 0020BF20; 002160B0 with 00215FE0) through the status
            pages lane's harness (tools/test_status_pages_reference.py);
            em_status_draw (00208AD0 / 00209280 / 00209860) against the
            original over each level-4 image's own inventory, health and
            battery (tools/test_status_draw_reference.py's structures);
            the status scene's inputs (em_status_scene_original: 0020E250,
            0020E3A0, 0020E1E0, 0020E460, 0020E6F0, 0020EC80) checked equal
            to the first-level inputs its own oracle test covers.

Method (lane and em_status_pages groups): the AREA01 UI lane's
(tools/test_area01_ui_reference.py via the status pages harness): the
oracle runs the original routine over a captured image; every callee
outside the routine's lane runs as ORIGINAL code too, nested, and its entry
is logged (address, stack pointer, the 64-bit integer argument registers
it takes, the float argument registers). The native translation runs over
a second copy; each callee it reaches through EmArea01Ui.call runs the same
ORIGINAL code in a second interpreter sharing the native memory, with every
register it is not passed poisoned. At every callee entry all 32 MiB of RAM
and the 16 KiB scratchpad must equal the oracle's at the same call; at the
end the call log, RAM and scratchpad must be identical. Every conditional
branch of the three lane routines must be taken both ways (EM_TEST_FULL=1
asserts it; the default run reports it).

Commands: (none) run; `mutants` the bounded mutation sweep of the review
(docs/AREA02_UI.md section 3). EM_TEST_FULL=1 runs every generated case and
every heavy callee as original code on both sides; the default runs a
covering sample with the heavy callees memoised / replayed, as the status
pages lane does. At most four worker processes (EM_TEST_JOBS=1: serial).
"""
import ctypes as C
import multiprocessing
import os
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
os.environ.setdefault('EM_LANE', 'area02_ui')
import reference_mode as RM  # noqa: E402
import test_status_pages_reference as SP  # noqa: E402
from test_player_slide_reference import EE, read_elf  # noqa: E402

R, U = SP.R, SP.U
MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
OUT = ROOT / 'build' / 'area02_ui'
SP.OUT = R.OUT = U.OUT = OUT
F = R.F
T = 0x810130                       # the status block D_00810130 (00209DF0's argument)
FREE = 0x1A00000                   # zero in every capture: crafted inputs live here
JOBS = R.JOBS

CAP = ROOT.parent / 'Extermination' / 'build' / 's87'
IMAGES = {  # name -> capture directory; see docs/AREA02_UI.md section 2
    'r1': CAP / 'route_a01r' / 'a01r_01_event',        # end of the revisit event: item 0x20 held (source of a01r_s0)
    'r1s0': CAP / 'route_a01r' / 'a01r_s0_pickup',     # after the event's pickup: t[4] = 9, D_00810CB4 = 90
    'r3': CAP / 'route_a01r' / 'a01r_03_door16',       # AREA02 arrival (source of a02_s0): health 90, infection 40
    'bed': CAP / 'route_a02' / 'a02_s0_mts_bed',       # after the bed: D_008106D0 = the bed record, item 0x20 used
    'a4': CAP / 'route_a02' / 'a02_04_panel',          # battery charge 0, infection 60
}
BEATS = list(IMAGES)

MINE = {0x209DF0: 0x9B0, 0x208750: 0x35C, 0x208AB0: 0x18}
MINE_PC = frozenset(pc for a, n in MINE.items() for pc in range(a, a + n, 4))
P, U8, U32, I32, U64 = C.POINTER, C.c_uint8, C.c_uint32, C.c_int32, C.c_uint64
MY_ENTRIES = {
    0x209DF0: ([U32], False, 'em_area02_ui'),
    0x208750: ([I32, U32, U32], False, 'em_area02_ui'),
    0x208AB0: ([U64, U64, U64, U32], True, 'em_area02_ui'),   # a0, a1, a2, f12 bits
}
MY_WSPEC = {  # this lane's callees not already in the status pages harness
    0x2082B0: (2, 0, None), 0x208AD0: (3, 0, None), 0x209280: (5, 0, None), 0x209860: (3, 0, None),
    0x1CC1E0: (7, 0, None), 0x122EF0: (2, 0, None), 0x207F80: (6, 0, None), 0x1D66A0: (4, 1, 'v0'),
}
MY_SOURCE = 'src/game/em_area02_ui.c'
REUSE_KINDS = ('00211970', '002121A0', '002125B0', '00212B60', '00212F30', '0020BF20', '002160B0', '00215FE0')


def install():
    """Extend the status pages harness with this lane's routines and images
    (module globals it reads at call time), then point the AREA01 UI
    harness at it."""
    SP.IMAGES = dict(IMAGES)
    SP.BEATS = list(BEATS)
    for a in MINE:
        SP.FAMILY[a] = 'a02'
    SP.TRPC = SP.TRPC | MINE_PC
    SP.ENTRIES.update(MY_ENTRIES)
    SP.WSPEC.update(MY_WSPEC)
    if MY_SOURCE not in SP.SOURCES:
        SP.SOURCES = SP.SOURCES + [MY_SOURCE]


# Quick mode only: float_to_int (001281C0, about 3,500 calls per 00209DF0
# case), the arc builder 002082B0, the three readouts and the sprite,
# rectangle, label and string callees are memoised on the oracle side and
# replayed on the native side after the entry check, the status pages
# lane's scheme (a recorded run is replayed only when every value it read,
# instruction fetches included, is unchanged). float_to_int's key is its
# only argument register f12 (and sp); every other callee keeps the status
# pages lane's full-register key. EM_TEST_FULL=1 runs them all as original
# code on both sides.
LIGHT = {0x1281C0, 0x2082B0, 0x208AD0, 0x209280, 0x209860, 0x207E40, 0x207F80, 0x1CC1E0, 0x122EF0, 0x123168}


def memo_run(e, address):
    if address != 0x1281C0:
        return SP.memo_run(e, address)
    key = (address, e.r[29] & MASK, e.f[12] & MASK)
    for reads, writes, v0, f0 in U.MEMO.get(key, ()):
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
    U.MEMO.setdefault(key, []).append((tuple(reads.items()), list(e.journal[j0:]), v0, f0))
    return v0, f0


def setup():
    install()
    SP.ELF = read_elf()
    SP.NATIVE = SP.build_native(tag='area02_ui')
    SP.install()
    if not RM.FULL:
        U.REPLAY = set(U.REPLAY) | LIGHT
        U.MEMO_ON = set(U.MEMO_ON) | LIGHT
        U.memo_run = memo_run
    R.TRACKER = R.build_tracker()
    SP.load_images()
    base = EE(SP.ELF)
    SP.check_code(base)
    for name in BEATS:                                  # this lane's code, as in every image
        ram = R.IMAGES[name][0]
        for start, size in MINE.items():
            assert ram[start:start + size] == bytes(base.mem[start:start + size]), (name, hex(start))
    missing = sorted(hex(a) for a in callee_set() if a not in SP.WSPEC and a not in U.INLINE)
    assert not missing, ('callees without a spec', missing)


def callee_set():
    probe = EE(SP.ELF)
    out = set()
    for pc in sorted(MINE_PC):
        word = probe.load(pc)
        if word >> 26 in (2, 3):
            target = (word & 0x3FFFFFF) << 2
            if target not in MINE:
                out.add(target)
    return out


def all_branches():
    probe = EE(SP.ELF)
    out = set()
    for pc in sorted(MINE_PC):
        word = probe.load(pc)
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        if op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or (op == 17 and rs == 8):
            if not (op == 4 and rs == 0 and rt == 0):
                out.add(pc)
    return out


# ======================================================================
# Running one lane case on both sides
# ======================================================================

def oracle_regs(address, args):
    """(integer argument registers, float argument registers) of an entry."""
    if address == 0x208AB0:
        return list(args[:3]), [args[3]]
    return list(args), []


def run_both(where, spad, steps, expect6=False):
    """steps: ('call', address, args) / ('poke', address, value, size), in
    order on both sides. expect6: the native side must stop with code 6 at
    00209DF0's call of 00209860 (docs section 1), after making exactly the
    oracle's calls before it with equal entries."""
    U.WSPEC = SP.hooks_for(steps)
    ee = U.oracle_ee(spad)
    U.oracle_hooks(ee)
    try:
        for step in steps:
            if step[0] == 'poke':
                ee.save(step[1], step[2], step[3])
                continue
            _, address, args = step
            regs, fregs = oracle_regs(address, args)
            for i, value in enumerate(regs):
                ee.r[4 + i] = (U.sx32(value) if value <= MASK else value) & MASK64
            for i, value in enumerate(fregs):
                ee.f[12 + i] = value & MASK
            ee.r[29], ee.r[31] = U.STACK_TOP, U.RETURN
            ee.run(address)
    except R.NotCompletable as ex:
        raise AssertionError((where, 'the original cannot complete this case') + ex.args)
    results_o = [ee.r[2] & MASK]
    native = SP.Native(spad, oracle=ee)
    results_n = []
    for step in steps:
        if step[0] == 'poke':
            native.poke(step[1], step[2], step[3])
            continue
        _, address, args = step
        rc, value = native.call(address, args)
        assert native.mismatch is None, (where, 'entry check') + native.mismatch
        f = native.state.core.fault
        if expect6:
            assert rc == -1 and f.code == 6 and f.address == 0x209DF0 and f.detail == 0x209860, \
                (where, 'expected code 6 at 00209860', rc, f.code, hex(f.address), hex(f.detail))
            k = next(i for i, c in enumerate(ee.calls) if c[0] == 0x209860)
            assert native.log == ee.calls[:k], (where, 'calls before 00209860', R.diff_logs(native.log, ee.calls[:k]))
            assert native.checked == k, (where, 'entry checks', native.checked, k)
            return ee.outcomes, k, native.checked
        assert rc == 0, (where, 'native faulted', hex(address), hex(f.address), f.code, hex(f.detail))
        results_n.append(value)
    if MY_ENTRIES[steps[-1][1]][1]:
        assert results_n[-1] == results_o[-1], (where, 'v0', hex(results_n[-1]), hex(results_o[-1]))
    assert native.log == ee.calls, (where, 'callee calls', R.diff_logs(native.log, ee.calls))
    assert native.checked == len(ee.entries), (where, 'entry checks', native.checked, len(ee.entries))
    assert native.ee.spad == ee.spad, (where, 'scratchpad differs (address, native, original)',
                                       R.first_differences(bytes(native.ee.spad), bytes(ee.spad), 0x70000000))
    bad = native.end_differences(ee)
    assert not bad, (where, 'RAM differs (address, native, original)', bad)
    return ee.outcomes, len(ee.calls), native.checked


put, w32 = R.put, R.w32


def case(beat):
    return R.case_ram(beat), bytearray(R.IMAGES[beat][1])


# ---- 00209DF0 -----------------------------------------------------------

# Infection values (D_0081085C): the captured one, the route's 0 / 40 / 60,
# the last float below 100, 100, just above, a truncating value, and ones
# whose conversion leaves 0..99 (the number path) or not.
INFECTION = [None, F(0.0), F(40.0), F(60.0), 0x42C7FFFF, F(100.0), 0x42C80001, F(99.5), F(-1.0), F(1000.0),
             0x80000000, F(100.9)]
HOVERS = [0, 1, 2, 3, 4, 5, 0xFF]
SELECTORS = [None, (2, 7), (0, 1), (1, 4), (0xFF, 0), (1, 5), (0xFF, 0xFF), (0, 3)]   # (D_00810CA4, D_00810CA6)


def hub_items():
    """Every hover, infection and selector value on every image, spread over
    combined items (each sets all three), plus a seeded random sample."""
    out = []
    for b, beat in enumerate(BEATS):
        for i in range(24):
            k = i + 5 * b
            out.append(('hub', beat, {'hover': HOVERS[k % len(HOVERS)], 'inf': INFECTION[k % len(INFECTION)],
                                      'sel': SELECTORS[k % len(SELECTORS)]}))
    rng = random.Random(0x209DF0)
    for _ in range(40):
        out.append(('hub', rng.choice(BEATS), {'hover': rng.choice(HOVERS), 'inf': rng.choice(INFECTION),
                                              'sel': rng.choice(SELECTORS), 'hp': rng.choice([F(90.0), F(100.0),
                                              F(34.0), F(0.0)])}))
    return out


def valid_selectors(sel):
    return sel is None or not (sel[0] != 2 and sel[1] > 4)


# the first item reaching 00209DF0's label path (infection exactly 100)
HUNDRED = next(i for i, it in enumerate(hub_items()) if it[2].get('inf') == F(100.0)
               and valid_selectors(it[2].get('sel')))


def hub_case(item):
    _, beat, knobs = item
    ram, spad = case(beat)
    if knobs.get('hover') is not None:
        put(ram, T + 0x11, knobs['hover'], 1)
    if knobs.get('inf') is not None:
        put(ram, 0x81085C, knobs['inf'])
    if knobs.get('hp') is not None:
        put(ram, 0x810858, knobs['hp'])
    sel = knobs.get('sel')
    expect6 = False
    if sel is not None:
        put(ram, 0x810CA4, sel[0], 1)
        put(ram, 0x810CA6, sel[1], 1)
    ca4, ca6 = ram[0x810CA4], ram[0x810CA6]
    expect6 = ca4 != 2 and ca6 > 4
    return run_both(('hub', beat, knobs), bytes(spad), [('call', 0x209DF0, (T,))], expect6=expect6)


# ---- 00208750 -----------------------------------------------------------

MARKER_XY = [  # 00209DF0's four rows (floats) and a few crafted ones
    (35584.0, 33952.0, 35584.0, 34496.0, 35584.0, 36096.0),
    (36288.0, 33536.0, 37376.0, 33536.0, 42496.0, 33536.0),
    (35584.0, 33120.0, 35584.0, 32576.0, 35584.0, 30976.0),
    (34880.0, 33536.0, 33792.0, 33536.0, 28672.0, 33536.0),
]
SPECIAL = [0x7F800000, 0xFF800000, 0x7FC00000, 0x7F7FFFFF, 0x00000001, 0x80000000, 0x4F000000, 0xCF000001]


def glow_items():
    out = []
    for beat in ('r3', 'bed'):
        for n in (16, 1, 2, 3, 0, -1, -3, 5):
            for rows in range(4):
                for table in (0x265540, 0x265570):
                    out.append(('glow', beat, {'n': n, 'xy': rows, 'rgb': table}))
    rng = random.Random(0x208750)
    for i in range(60):
        xy = [rng.choice([F(rng.uniform(-40000, 40000)), rng.choice(SPECIAL), F(rng.uniform(-3, 3))])
              for _ in range(12)]
        rgb = [rng.choice([rng.randrange(0, 256), rng.randrange(-2 ** 31, 2 ** 31) & MASK, 0x7FFFFFFF, 0x80000000])
               for _ in range(12)]
        offs = None
        if i % 3 == 0:          # the offset table: zero / nonzero pairs in both halves
            offs = [rng.choice([0, 0, 1, -1, 2, -2, 0x7FFFFFFF]) & MASK for _ in range(18)]
        where = rng.choice(['ram', 'spad'])
        out.append(('glow', rng.choice(BEATS), {'n': rng.choice([16, 2, 3, 1, 7]), 'xyw': xy, 'rgbw': rgb,
                                               'offs': offs, 'where': where, 'cursor': i % 2}))
    return out


def glow_case(item):
    _, beat, knobs = item
    ram, spad = case(beat)
    xy, rgb = FREE, FREE + 0x100
    if 'xyw' in knobs:
        words, rgbw = knobs['xyw'], knobs['rgbw']
        if knobs['where'] == 'spad':
            xy = 0x70003000
            for i, v in enumerate(words):
                struct.pack_into('<I', spad, 0x3000 + 4 * i, v)
        else:
            for i, v in enumerate(words):
                put(ram, xy + 4 * i, v)
        for i, v in enumerate(rgbw):
            put(ram, rgb + 4 * i, v)
    else:
        row = MARKER_XY[knobs['xy']]
        for r in range(3):
            put(ram, xy + 16 * r, F(row[2 * r]))
            put(ram, xy + 16 * r + 4, F(row[2 * r + 1]))
        rgb = knobs['rgb']
    if knobs.get('offs'):
        for i, v in enumerate(knobs['offs']):
            put(ram, 0x265160 + 4 * i, v)
    if knobs.get('cursor') or knobs['n'] < 0:
        ctx = w32(ram, 0x275670)
        put(ram, ctx + 0x14, FREE + 0x10000)     # a packet area no capture uses
    return run_both(('glow', beat, knobs), bytes(spad), [('call', 0x208750, (knobs['n'], xy, rgb))])


# ---- 00208AB0 -----------------------------------------------------------

def fan_items():
    out = []
    rng = random.Random(0x208AB0)
    for beat in ('r1s0', 'r3'):
        for ang in (0.0, 1.0, -2.5, 3.14159, 100.0):
            out.append(('fan', beat, {'pos': (2224.0, 2096.0, 28.0, 28.0), 'col': 16, 'f12': F(ang)}))
    for _ in range(12):
        out.append(('fan', rng.choice(BEATS), {'pos': tuple(rng.uniform(1500, 2500) for _ in range(2)) +
                                                     (rng.uniform(28, 44), 28.0),
                                              'col': rng.randrange(0, 40), 'f12': F(rng.uniform(-7, 7)),
                                              'hi': rng.choice([0, 0xFFFFFFFF00000000])}))
    return out


def fan_case(item):
    _, beat, knobs = item
    ram, spad = case(beat)
    pos, col = FREE, FREE + 0x10
    for i, v in enumerate(knobs['pos']):
        put(ram, pos + 4 * i, F(v))
    for i in range(3):
        put(ram, col + 4 * i, knobs['col'])
    ctx = w32(ram, 0x275670)
    put(ram, ctx + 0x14, FREE + 0x10000)
    hi = knobs.get('hi', 0)
    # a2 as 0020AC70 passes it; a0/a1 upper halves as images (the EE passes the whole register)
    return run_both(('fan', beat, knobs), bytes(spad),
                    [('call', 0x208AB0, (pos | hi, col, 0x273580, knobs['f12']))])


# ---- reuse: em_status_pages over the level-4 images --------------------------

def reuse_pages_items():
    """The status pages lane's designed cases for the reused routines,
    retargeted from the AREA11 images to the level-4 ones, plus the real
    level-4 call shapes."""
    out = []
    src = [it for it in SP.spr4_items() + SP.item_items() if it[0] in REUSE_KINDS and it[1] == 'hub']
    for i, (kind, _, knobs) in enumerate(src):
        out.append((kind, BEATS[i % len(BEATS)], dict(knobs, retarget=1)))
    # a01r_s0: the take posts B0 = 1, B1 = 0x10 (ammunition); the page opens
    # on the pickup's source image and runs its states (the end image has t[4] = 9).
    for b1 in range(0x10, 0x17):
        out.append(('00211970', 'r1', {'t': {4: (0, 1)}, 'b': [(0x8106B0, 1, 1), (0x8106B1, b1, 1)]}))
    for e74 in (0, 0x20, 0x40):
        out.append(('00211970', 'r1s0', {'pads': (0, e74, 0)}))
    # a02_s0: HEALING on item 0x20 with the captured inventory; the bed's
    # use-item request (B0 = 4) with the real bed record D_008106D0 and the
    # counts as they were before the use (item 0x20 = 1, infection 40, health 90).
    out.append(('002160B0', 'r3', {'b': [(0x8106B0, 1, 1), (0x8106B1, 0x20, 1)], 't': {5: (0, 1)}}))
    out.append(('002160B0', 'r1', {'b': [(0x8106B0, 1, 1), (0x8106B1, 0x20, 1)], 't': {5: (0, 1)}}))
    bed_before = [(0x810C84, 1, 1), (0x81085C, F(40.0), 4), (0x810858, F(90.0), 4)]
    out.append(('002160B0', 'bed', {'b': bed_before + [(0x8106B0, 4, 1)], 't': {5: (0, 1)}}))
    for e74 in (0, 0x20, 0x40):
        for e78 in (0, 0x1000, 0x4000):
            out.append(('002160B0', 'bed', {'b': bed_before, 'pads': (0, e74, e78), 'ring': ([2], 1),
                                            't': {5: (1, 1), 0x17: (0, 1), 0x19: (0, 1)}}))
    return out


def reuse_pages_case(item):
    """A retargeted case whose inputs were designed for the status-hub
    image may be one the ORIGINAL cannot complete on a level-4 image (it
    follows a pointer the case did not set up to an unmapped address): it
    is skipped and counted. The level-4 call shapes must complete."""
    kind = item[0]
    try:
        if kind == '002160B0' or kind == '00215FE0':
            return SP.item_case(item)
        return SP.spr4_case(item)
    except AssertionError as ex:
        if item[2].get('retarget') and len(ex.args[0]) > 1 and ex.args[0][1] == 'the original cannot complete this case':
            return 'skipped'
        raise


# ---- reuse: em_status_draw over the level-4 inventories ---------------------

def reuse_draw():
    """em_status_draw (health 00208AD0, battery 00209280, ammunition 00209860)
    against the original over each level-4 image, with its own values."""
    import test_status_draw_reference as SD
    from export_status_hub import Original, UI
    from test_point_light_reference import number
    out = OUT / 'status_draw'
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('draw.dylib' if sys.platform == 'darwin' else 'draw.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-fPIC',
                    '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc', 'src/game/em_status_draw.c',
                    '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    native.em_status_health_draw.argtypes = [C.POINTER(C.c_uint32), C.c_float, C.c_uint8, C.c_int, C.c_int,
                                             C.POINTER(SD.Data), C.POINTER(SD.Workers)]
    native.em_status_battery_draw.argtypes = [C.c_uint16, C.c_uint8, C.c_uint8, C.c_int, C.c_int, C.c_uint64,
                                              C.c_int, C.POINTER(SD.BatteryData), C.POINTER(SD.Workers)]
    native.em_status_ammo_draw.argtypes = [C.POINTER(SD.AmmoInventory), C.c_int, C.c_int,
                                           C.POINTER(SD.AmmoData), C.POINTER(SD.Workers)]
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    calls = []

    def emit(event):
        calls.append(event)
        return 1
    workers = SD.Workers(None, SD.Blend(lambda _, m: emit(('blend', m))),
                         SD.Rect(lambda _, x, y, x1, y1, c: emit(('rectangle', x, y, x1, y1, c))),
                         SD.Text(lambda _, p, x, y, w, h, s, c: emit(('text', p, x, y, w, h, s.decode('latin1'), c))),
                         SD.Arc(lambda _, a: emit(('arc', C.string_at(a, 96)))),
                         SD.Sprite(lambda _, x, y, w, h, c, t: emit(('sprite', x, y, w, h, c, t))))
    cases = commands = 0
    for beat in BEATS:
        ram = R.IMAGES[beat][0]
        o = Original(elf, ram, 0, 0, True)
        d = SD.Data()
        for i, at in enumerate(range(0x265390, 0x265510, 0x60)):
            d.arcs[i] = (C.c_float * 24).from_buffer_copy(o.read(at, 96))
        d.white, d.red = o.load(0x265510, 8), o.load(0x265528, 8)
        d.label = o.string_bytes(o.load(0x267298))
        o.run(0x1cc170, (o.load(0x267298),))
        d.label_width = o.r[2]
        d.warning_max, d.normal_max, d.separator = [o.string_bytes(p) for p in (0x273558, 0x273560, 0x273568)]
        health = number(w32(ram, 0x810858))
        warning = ram[0x8104E4]
        for counter in (0, 1, 59, 60, w32(ram, UI + 0x20)):
            o = Original(elf, ram, 0, 0, True)
            o.save(UI + 0x20, counter)
            wanted = SD.capture_calls(o)
            o.run(0x208ad0, (UI, 208, 196))
            calls.clear()
            got = C.c_uint32(counter)
            assert native.em_status_health_draw(C.byref(got), health, warning, 208, 196, C.byref(d),
                                                C.byref(workers)) == 1
            assert got.value == o.load(UI + 0x20) and calls == wanted, ('health', beat, counter)
            cases += 1
            commands += len(calls)
        charge = struct.unpack_from('<H', ram, 0x810CB2)[0]
        capacity, equipped = ram[0x810CB7], ram[0x810C7F]
        battery_data = SD.BatteryData(d.white, o.string_bytes(o.load(0x26729c)), d.separator)
        for compact in (0, 1):
            o = Original(elf, ram, 0, 0, True)
            wanted = SD.capture_calls(o)
            o.run(0x209280, (UI, 16, 118, 0x2004512515422288, compact))
            calls.clear()
            assert native.em_status_battery_draw(charge, capacity, equipped, 16, 118, 0x2004512515422288, compact,
                                                 C.byref(battery_data), C.byref(workers)) == 1
            assert calls == wanted, ('battery', beat, compact)
            cases += 1
            commands += len(calls)
        inv = SD.AmmoInventory(ram[0x810CA4], ram[0x810CA6],
                               (C.c_int16 * 5)(*struct.unpack_from('<5h', ram, 0x810CA8)),
                               struct.unpack_from('<h', ram, 0x810CB4)[0])
        ammo_data = SD.AmmoData(d.white, o.string_bytes(o.load(0x2672a0)), o.string_bytes(0x273570))
        o = Original(elf, ram, 0, 0, True)
        wanted = SD.capture_calls(o)
        o.run(0x209860, (UI, 16, 190))
        calls.clear()
        assert native.em_status_ammo_draw(C.byref(inv), 16, 190, C.byref(ammo_data), C.byref(workers)) == 1
        assert calls == wanted, ('ammo', beat)
        cases += 1
        commands += len(calls)
    return cases, commands


# ---- reuse: the status scene's inputs --------------------------------------

SCENE_INPUTS = (('D_00810CA4..A7 (equipment letters, 0020E250)', 0x810CA4, 4),
                ('D_008104E4 (0020EC80 warning)', 0x8104E4, 1),
                ('D_00810C60 (0020EC80 costume)', 0x810C60, 1))


def reuse_scene():
    """em_status_scene_original's inputs in every level-4 image equal the
    first-level captures' (tools/test_status_scene_reference.py runs its
    oracle over those): the same letters, variant and costume."""
    ref = (ROOT.parent / 'Extermination/build/startup-reference/status-hub/eeMemory.bin').read_bytes()
    checked = 0
    for beat in BEATS:
        ram = R.IMAGES[beat][0]
        for name, at, n in SCENE_INPUTS:
            assert ram[at:at + n] == ref[at:at + n], ('scene input differs from the first level', beat, name,
                                                      ram[at:at + n].hex(), ref[at:at + n].hex())
            checked += 1
    return checked


# ======================================================================
# Faults, the s0 measurement, groups and main
# ======================================================================

def default_args(address):
    return {0x209DF0: (T,), 0x208750: (16, FREE, 0x265540), 0x208AB0: (FREE, FREE + 0x10, 0x273580, F(1.0))}[address]


def fault_items():
    """NULL call, a latched fault, no views and a failing callee at call
    positions (quick: the first, second and last; full: a spread of every
    position) of each lane entry; 00208750 with n <= 0 calls nothing, so a
    NULL call is not a fault there."""
    out = []
    for address in MINE:
        out += [('null', address), ('latched', address), ('noviews', address)]
        out += [('fail', address, k) for k in (0, 1, -1)]
        if RM.FULL:
            out += [('fail', address, k) for k in range(2, 4000, 97)]
    out.append(('null0', 0x208750))
    return out


def fault_case(item):
    kind, address = item[0], item[1]
    ram, spad = case('r3')
    args = default_args(address)
    if kind == 'null0':
        nat = SP.Native(bytes(spad), oracle=None, null_call=True)
        rc, _ = nat.call(0x208750, (0, FREE, 0x265540))
        assert rc == 0 and nat.state.core.fault.code == 0, ('null call, n = 0', rc)
        return 1
    if kind == 'null':
        nat = SP.Native(bytes(spad), oracle=None, null_call=True)
        rc, _ = nat.call(address, args)
        assert rc == -1 and nat.state.core.fault.code == 1, ('null call', hex(address), rc)
        assert nat.unchanged(bytes(spad)), ('null call wrote', hex(address))
        return 1
    if kind == 'latched':
        nat = SP.Native(bytes(spad), oracle=None)
        nat.state.core.fault.code = 2
        rc, _ = nat.call(address, args)
        assert rc == -1 and nat.log == [] and nat.unchanged(bytes(spad)), ('latched', hex(address))
        return 1
    if kind == 'noviews':
        nat = SP.Native(bytes(spad), oracle=None, views=False)
        rc, _ = nat.call(address, args)
        if address == 0x208AB0:     # reads no memory itself: the call happens
            assert rc == 0 and nat.state.core.fault.code == 0 and len(nat.log) == 1, ('no views', hex(address))
        else:
            assert rc == -1 and nat.state.core.fault.code == 4, ('no views', hex(address), nat.state.core.fault.code)
        return 1
    k = item[2]
    if k < 0:
        full = SP.Native(bytes(spad), oracle=None)
        rc, _ = full.call(address, args)
        assert rc == 0
        total = len(full.log)
        k = total - 1
        ram, spad = case('r3')
    nat = SP.Native(bytes(spad), oracle=None, fail_at=k)
    rc, _ = nat.call(address, args)
    if k >= len(nat.log) and rc == 0:     # fewer calls than k (00208AB0 has one)
        return 0
    assert rc == -1 and nat.state.core.fault.code == 2 and len(nat.log) == k + 1, ('fail', hex(address), k)
    assert nat.state.core.fault.address == nat.log[-1][0], ('fail address', hex(address), k)
    return 1


def s0_measurement():
    """What the code-6 path would draw: run the original 00209DF0 with an
    invalid secondary selector and record the texture word 00209860 hands
    its last 00207E40 call (docs section 1: 00209DF0's loop counter s0 = 4)."""
    ram, spad = case('r3')
    put(ram, 0x810CA4, 0, 1)
    put(ram, 0x810CA6, 5, 1)
    ee = U.oracle_ee(bytes(spad))
    seen = []

    def hook(e):
        ra = e.r[31] & MASK
        if 0x209860 <= ra < 0x209860 + 0x584:
            seen.append(e.r[10] & MASK64)
        U.SAR.nested_bits(e, 0x207E40)
    heavy = {0x20AC70, 0x20A7A0}

    def skip(e):
        pass
    ee.hooks = {0x207E40: hook}
    for a in heavy:
        ee.hooks[a] = skip
    ee.r[4] = T
    ee.r[29], ee.r[31] = U.STACK_TOP, U.RETURN
    ee.run(0x209DF0)
    assert seen and seen[-1] == 4, ('00209860 texture word', [hex(v) for v in seen])
    return seen[-1]


# Branch outcomes no input reaches (docs/AREA02_UI.md section 3).
UNREACHED = {
    # 00209DF0's wheel switch tests i == 0, 2, 1, then 3; i is the loop
    # counter 0..3, so the last test never fails
    (0x20A1D4, False),
}

POOL = None


def pmap(fn, items):
    items = list(items)
    if POOL is None or len(items) < 2:
        return [fn(x) for x in items]
    size = max(1, -(-len(items) // (3 * JOBS)))
    return list(POOL.imap(fn, items, chunksize=size))


def lane_groups():
    hub = hub_items()
    glow = glow_items()
    fan = fan_items()
    return [
        ('00209DF0', RM.select(hub, 10, 0xDF0, axes=(lambda it: it[1], lambda it: it[2].get('sel'),
                                                   lambda it: it[2].get('hover')),
                               keep=lambda i, it: i == HUNDRED), len(hub), hub_case),
        ('00208750', RM.select(glow, 30, 0x750, axes=(lambda it: it[2]['n'], lambda it: it[1],
                                                    lambda it: it[2].get('where')),
                               keep=lambda i, it: bool(it[2].get('offs'))), len(glow), glow_case),
        ('00208AB0', RM.select(fan, 6, 0xAB0, axes=(lambda it: it[1],)), len(fan), fan_case),
    ]


def reuse_groups():
    pages = reuse_pages_items()
    return [('reuse em_status_pages', RM.select(pages, 40, 0x2F0, axes=(lambda it: it[0], lambda it: it[1]),
                                                keep=lambda i, it: i >= len(pages) - 22),
             len(pages), reuse_pages_case)]


def main():
    global POOL
    t0 = time.time()
    setup()
    tex = s0_measurement()
    t1 = time.time()
    if JOBS > 1 and 'fork' in multiprocessing.get_all_start_methods():
        POOL = multiprocessing.get_context('fork').Pool(JOBS)
    outcomes, counts, calls, entries, cases, skipped = set(), [], 0, 0, 0, 0
    try:
        # the fault cases run beside the groups (two of them are whole native runs)
        pending = POOL.map_async(fault_case, fault_items(), chunksize=1) if POOL is not None else None
        faults = sum(fault_case(it) for it in fault_items()) if pending is None else None
        for name, items, total, fn in lane_groups() + reuse_groups():
            before, tg = cases, time.time()
            for r in pmap(fn, items):
                if r == 'skipped' and fn is reuse_pages_case:
                    skipped += 1
                    continue
                if r in ('unmeasured', 'skipped'):
                    raise AssertionError((name, r))
                cases += 1
                outcomes.update(r[0])
                calls += r[1]
                entries += r[2]
            counts.append(RM.part(cases - before, total, name + ' cases'))
            if os.environ.get('EM_A02UI_TIMES'):
                print('%-28s %4d cases %.1f s' % (name, len(items), time.time() - tg), flush=True)
        if pending is not None:
            faults = sum(pending.get())
    finally:
        if POOL is not None:
            R.close_pool(POOL)
            POOL = None
    tg = time.time()
    draw_cases, draw_commands = reuse_draw()
    scene = reuse_scene()
    if os.environ.get('EM_A02UI_TIMES'):
        print('reuse draw + scene %.1f s; setup + faults %.1f s' % (time.time() - tg, t1 - t0), flush=True)
    branches = all_branches()
    need = {(pc, t) for pc in branches for t in (True, False)}
    got = {o for o in outcomes if o[0] in MINE_PC}
    missing = sorted((hex(pc), t) for pc, t in need - got if (pc, t) not in UNREACHED)
    RM.banner(*counts, f'{cases} oracle cases ({skipped} retargeted reuse cases the original cannot complete '
              f'on a level-4 image, skipped), {calls} callee calls, {entries} entries with RAM + scratchpad + '
              'arguments equal to the original\'s',
              f'reuse em_status_draw: {draw_cases} cases, {draw_commands} ordered commands; '
              f'status scene inputs: {scene} checks',
              f'{len(branches)} lane branches: {len(need & got)} of {len(need)} outcomes seen, '
              f'{len(UNREACHED)} unreachable by construction',
              f'{faults} fault cases; code-6 texture word measured: {tex}')
    if RM.FULL or os.environ.get('EM_A02UI_REQUIRE_COVER'):
        assert not missing, ('branch outcomes never seen', missing)
    elif missing:
        print('outcomes not in this sample (EM_TEST_FULL=1 requires all):', missing)
    print('elapsed %.1f s' % (time.time() - t0))
    print('area02 ui reference: PASS')


# ======================================================================
# The bounded mutation sweep (review)
# ======================================================================

MUTANTS = [  # (name, text, replacement): one operation each in em_area02_ui.c
    ('t not reset per copy', '        uint32_t t = 0;\n', '        static uint32_t t = 0;\n'),
    ('u = 1 - t -> t', 'const uint32_t u = ui_fsub(UI_F_ONE, t);', 'const uint32_t u = t;'),
    ('xy rows as ints', 'const uint32_t x0 = ui_lw(s, xy + 4u * k);',
     'const uint32_t x0 = ui_fcvt((int32_t)ui_lw(s, xy + 4u * k));'),
    ('no halving', 'if (dx != 0 || dy != 0)', 'if (dx != 0 && dy != 0)'),
    ('logical halving', '(uint32_t)((int32_t)ui_lw(s, d + 4u * k) >> 1)', '(ui_lw(s, d + 4u * k) >> 1)'),
    ('offset subtracted', 'ui_fadd(off, sum)', 'ui_fsub(sum, off)'),
    ('middle weight not doubled', 'ui_fmul(UI_F_TWO, ui_fmul(u, t))', 'ui_fmul(u, t)'),
    ('count', '(2u * un + 2u) & 0xFFFFu', '(2u * un + 1u) & 0xFFFFu'),
    ('tag n zero-extended', "0x8000u | ui_sx(un)", '0x8000u | (uint64_t)un'),
    ('step n', 'ui_fcvt((int32_t)(un - 1u))', 'ui_fcvt((int32_t)un)'),
    ('word 6', 'ui_sw(s, d + 0x18u, 0xFFFFFFu);', 'ui_sw(s, d + 0x18u, 0xFFFFFFFFu);'),
    ('hover off by one', 'ui_lbu(s, ui + 0x11u) == k + 1u', 'ui_lbu(s, ui + 0x11u) == k'),
    ('wheel selected off by one', 'hover != 0 && i == hover - 1u', 'hover != 0 && i == hover'),
    ('wheel extents', '{0x1B0, 0x108}', '{0x1B0, 0x10A}'),
    ('infection compare', 'if (n == 0x64)', 'if ((uint32_t)n == 0x65)'),
    ('formatter width', 'uint64_t fmt[3] = {n, 3, 1}', 'uint64_t fmt[3] = {n, 4, 1}'),
    ('selector gate', 'primary != 2 && secondary > 4', 'primary != 2 && secondary > 5'),
    ('dots order', 'const uint64_t a = f2i(s, 0x46E24000u);', 'const uint64_t a = f2i(s, 0x46E2C000u);'),
    ('trail base', 'ui_sw(s, A2_SPAD_38A0 + 4u, 0x43880000u);', 'ui_sw(s, A2_SPAD_38A0 + 4u, 0x43800000u);'),
    ('angle step', '#define F_90 0x42B40000u', '#define F_90 0x42B00000u'),
    ('fan slot', 'uint64_t a[4] = {1, a0, a1, a2}', 'uint64_t a[4] = {0, a0, a1, a2}'),
    ('fan f12 dropped', 'ui_call(s, A2_001D66A0, 4, a, 1, &f12, &r, NULL);', '(void)f12; ui_call(s, A2_001D66A0, 4, a, 0, NULL, &r, NULL);'),
]


def mutant_item(arg):
    fn, it = arg
    try:
        fn(it)
        return False
    except AssertionError:
        return True


def mutants():
    """Apply each mutation to a copy of em_area02_ui.c, rebuild, and run the
    default lane cases: every mutant must be killed. Each mutant (and the
    unmutated control, which must survive) gets its own source file and its
    own library path: dlopen returns the image already loaded from a path,
    so a shared path would run the first mutant for every later one."""
    setup()
    text = (ROOT / MY_SOURCE).read_text()
    mdir = OUT / 'mut'
    mdir.mkdir(parents=True, exist_ok=True)
    groups = lane_groups()
    loaded, survivors = set(), []
    for index, (name, old, new) in enumerate([('control', None, None)] + MUTANTS):
        if old is not None:
            assert text.count(old) == 1, ('mutation site', name)
        slug = '%02d_%s' % (index, ''.join(ch if ch.isalnum() else '_' for ch in name))
        mut = mdir / ('em_area02_ui_' + slug + '.c')
        mut.write_text(text if old is None else text.replace(old, new))
        srcs = [s for s in SP.SOURCES if s != MY_SOURCE] + [str(mut)]
        tag = 'area02_ui_mut_' + slug
        assert tag not in loaded, ('library path reused', tag)
        loaded.add(tag)
        U.NATIVE = SP.NATIVE = SP.build_native(sources=srcs, tag=tag)
        killed = None
        for gname, items, _, fn in groups:
            for it in items:
                try:
                    fn(it)
                except AssertionError:
                    killed = gname
                    break
            if killed:
                break
        print('%-28s %s' % (name, ('killed by ' + killed) if killed else 'SURVIVED'), flush=True)
        if old is None:
            assert not killed, ('the unmutated control was killed', killed)
        elif not killed:
            survivors.append(name)
    print('mutants: %d of %d killed (control survived)' % (len(MUTANTS) - len(survivors), len(MUTANTS)))
    assert not survivors, survivors


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'mutants':
        mutants()
    else:
        main()
