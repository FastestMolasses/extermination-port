#!/usr/bin/env python3
"""The sixteenth level's boot workers and overlay nodes (src/game/em_level16_boot.c)
against the ORIGINAL instructions. Unwired.

The harness is tools/test_level14_port_reference.py (`O`), configured as
test_level16_beast_reference.py configures it. Every entry runs as original
code from the user's ELF or overlay over a fork recording; 0x823780, 0x825040,
0x8251C0, 0x825280 and 0017C370 run inside their callers at the top level of
the oracle, as they are inlined in the translation (PIECES holds the first
four so their words count too); every other callee is a hook, and the helpers
in MATH run as original code nested in the oracle. Compared per case: every
call and its arguments, memory at every call entry, the memory accesses between
calls one for one in order, memory after the last store, the result, a poisoned
re-run, coverage of every reachable original word, the callee inventory and the
fail-stop contract.

Two recordings: a03b_03_item2a (AREA03 resident) for the boot entries and
the AREA03 nodes, a04c_01_door73 (AREA04 resident) for the AREA04 nodes and a
sample of the boot entries. Both overlays' text is checked against the user's
extract/OVERLAY files. The designed nodes sit in zero RAM at FREE. A second
pass (the level-14 module's own hook table) runs 001ED450 of
em_level14_port_boot.c.

EM_TEST_FULL=1 widens the random sample and runs the whole contract.
EM_LEVEL16_BOOT_ONLY=<label prefix> runs a subset (no coverage or contract);
EM_LEVEL16_BOOT_SOURCE=<dir> tests another copy of the module sources;
EM_LEVEL16_BOOT_SHOW=<n> prints n failures; EM_LEVEL16_BOOT_TRACE=<label>
prints the original's calls and accesses of matching cases (no instruction
text). Reports hold addresses and counts only.
"""
import ctypes as C
import os
import random
import re
from pathlib import Path
import gen_level16_boot as G
import test_level14_port_reference as O
import reference_root as refs
ROOT = Path(__file__).resolve().parents[1]
U, I, F, Q, S = O.U, O.I, O.F, O.Q, O.S
FUNCS = {
    0x001B3670: ('em_level16_boot_001B3670', 348, [U, U, U, S], I),
    0x001B3C30: ('em_level16_boot_001B3C30', 736, [U, U, U, F, F, S], None),
    0x001BD460: ('em_level16_boot_001BD460', 256, [U], I),
    0x001C82A0: ('em_level16_boot_001C82A0', 476, [U, U], None),
    0x00158430: ('em_level16_boot_00158430', 344, [U], None),
    0x0017C370: ('em_level16_boot_0017C370', 208, [U], None),
    0x0021BED0: ('em_level16_boot_0021BED0', 184, [U], I),
    0x0021BF90: ('em_level16_boot_0021BF90', 176, [U, U], None),
    0x0021C040: ('em_level16_boot_0021C040', 212, [U, U], None),
    0x008235A0: ('em_level16_boot_a03_008235A0', 0x1D8, [U], None),
    0x00823810: ('em_level16_boot_a03_00823810', 0x114, [U], None),
    0x00825200: ('em_level16_boot_a03_00825200', 0x84, [U, S], None),
    0x00824EF0: ('em_level16_boot_a04_00824EF0', 0x14C, [U, S], None),
    0x00825310: ('em_level16_boot_a04_00825310', 0x200, [U], None),
}
BEAT3, BEAT4 = 'a03b_03_item2a', 'a04c_01_door73'
# original functions inlined in the translation: their words count too
PIECES = {0x823780: (0x84, BEAT3), 0x825040: (0x178, BEAT4), 0x8251C0: (0xB4, BEAT4), 0x825280: (0x8C, BEAT4)}
AREA04_ENTRIES = (0x824EF0, 0x825310)
FREE = 0x1E61000
A = FREE + 0x1000            # the designed node
B = FREE + 0x2000            # the second record (target, matrix, hit, pose)
AUX = FREE + 0x3000          # what the pointers reach
CONTACT = AUX + 0x800
NODE = AUX + 0x1000
CALLBACK = 0x1CAA00          # the image's +0x4C method of kind 7 nodes
MODE, AREA_BYTE, ROOM = 0x70003B8D, 0x810700, 0x810702
FLAG, COUNT, FLAG4, COUNT4, SEEN = 0x810781, 0x810801, 0x810780, 0x810800, 0x810821
MATH = frozenset((0x1026A0, 0x1026D0, 0x102738, 0x102760, 0x1028B8, 0x1028D0, 0x102948, 0x102958, 0x1029C0,
                  0x102C58, 0x103230, 0x11E420, 0x1281C0, 0x1B1470))
SELF_ENTRIES = {0x158430, 0x8235A0, 0x823810, 0x824EF0}
DEAD = set()


def spec(args):
    words, floats, out = 4, 12, []
    for kind in args:
        if kind == F:
            out.append((F, floats))
            floats += 1
        else:
            out.append((kind, words))
            words += 1
    return out


HOOKS = [(f'w_{a:08X}', a, spec(args), result) for a, (args, result) in sorted(G.HOOKS.items())]
LEVEL14 = (O.HOOKS, O.Hooks, O.SOURCES)


def default_args(entry, kinds):
    if entry == 0x1B3670:
        return [A, B, B + 0x100, O.STACK_TOP]
    if entry == 0x1B3C30:
        return [A, B, 0x808080, O.fb(40.), O.fb(5.), O.STACK_TOP]
    return {1: [A], 2: [A, B]}[len(kinds)] if S not in kinds else [A, O.STACK_TOP]


def beat_of(entry):
    return BEAT4 if entry in AREA04_ENTRIES else BEAT3


def configure():
    O.FREE = FREE
    O.FUNCS, O.HOOKS = FUNCS, HOOKS
    O.OVERLAY_OF = {}
    O.OVERLAY_FILES = {4: ROOT.parent / 'Extermination/extract/OVERLAY/AREA03.BIN',
                       5: ROOT.parent / 'Extermination/extract/OVERLAY/AREA04.BIN'}
    O.OUT = ROOT / 'build/levels/level16_boot'
    O.SOURCES = ('em_level16_boot.c',)
    O.BEATS = [BEAT3, BEAT4]
    O.BEAT_DIRS = {BEAT3: refs.capture('levels-recordings-20261030-1/s87/route_a03b/' + BEAT3),
                   BEAT4: refs.capture('levels-recordings-20261030-1/s87/route_a04c/' + BEAT4)}
    O.RESIDENT = {BEAT3: 4, BEAT4: 5}
    O.BY_ADDRESS = {a: (n, args, res) for n, a, args, res in HOOKS}
    O.BY_NAME = {n: a for n, a, _, _ in HOOKS}
    O.HOOK_NAMES = [h[0] for h in HOOKS] + ['w_callback']
    O.Hooks = type('Hooks', (C.Structure,), {'_fields_':
        [('ctx', C.c_void_p), ('bytes', O.BYTES_FN)] +
        [(name, O.hook_proto(args, res)) for name, _, args, res in HOOKS] + [('w_callback', O.CALLBACK_FN)]})
    O.SELF_ENTRIES = set(SELF_ENTRIES)
    O.HEAVY = set()
    O.RETURN_SITES = O.RETURN_SITES_FULL = ()
    O.CONTRACT_UNREACHABLE = {}
    O.LOCAL_STORES = {0x1B3C84}   # 001B3C30 keeps the reach in a saved float register and stores it to its frame
    O.DEFAULT_ARGS = {e: default_args(e, k) for e, (_, _, k, _) in FUNCS.items()}
    O.image_for = lambda entry: O.CAPTURES[PIECES[entry][1] if entry in PIECES else beat_of(entry)][0]
    O.CAPTURES.clear()
    O.CAPTURES.update(O.load_captures(O.read_elf()))
    O.JUMP_TABLES = {}
    O.TABLE_BYTES = frozenset()


NAN, INF, NINF, TINY = 0x7FC00000, 0x7F800000, 0xFF800000, 1
FLOATS = [O.fbits(v) for v in (0., 1., -1., .2, .1, .3, .5, 2., 1.9, 2.1, 2.2, 3., -3., 25., 24.9)] + \
         [0x80000000, NAN, INF, NINF, TINY, 0x80000001]
BYTES = (0, 1, 2, 3, 4, 5, 6, 7, 0x7F, 0x80, 0xFF)
RESULTS = (0, 0, 1, 1, 2, 0x100, -1)


def common():
    b, h, w, f = O.b8, O.h16, O.w32, O.f32b
    return [(A + 0x4C, w(CALLBACK)), (A + 0xB0, O.fw(2., 3., 4., 1.)), (A + 0xC4, f(.5)),
            (B, O.fw(5., 6., 7., 1.)), (B + 0x30, O.fw(8., 9., 10., 1.)), (B + 0x100, O.fw(1., 2., 3., 1.)),
            (0x275B40, w(AUX)), (AUX + 4, w(AUX + 0x100)), (AUX + 8, w(AUX + 0x200)), (AUX + 0xC, w(AUX + 0x300)),
            (AUX + 0x10, w(AUX + 0x400)), (0x700031D0, w(CONTACT)), (CONTACT + 0x24, O.fw(0., 1., 0., 2.)),
            (0x700031B0, O.fw(11., 12., 13., 1.))]


def fields(rng):
    """The fields the entries branch on, each a random interesting value."""
    b, h, w = O.b8, O.h16, O.w32
    c = rng.choice
    return [
        (A, b(c((0, 1, 2)))), (A + 4, b(c((0, 1, 1, 1, 2, 3, 4)))), (A + 5, b(c((0, 0, 1, 1, 2)))),
        (A + 0xA, b(c((0, 1)))), (A + 0xB, b(c((0, 4, 3, 0xFF)))), (A + 0xD, b(c(BYTES))),
        (A + 0x2E, h(c((0, 1, 2, 7, 8, 0x100, 0xFFFF)))), (A + 0x36, h(c((0, 1, 0x8000)))),
        (A + 0x9A, b(c(BYTES))), (A + 0x1F0, b(c((0, 5, 6, 7, 8, 0x3C)))), (A + 0x1F1, b(c((0, 1, 3)))),
        (A + 0x220, w(c(FLOATS))), (A + 0x235, b(c(BYTES))), (A + 0x236, b(c((0, 1)))), (A + 0x20E, h(c((0, 1, 0x8000)))),
        (B + 3, b(c(BYTES))), (B + 0x20, w(c(FLOATS))), (A + 0x10, w(c(FLOATS))), (A + 0x14, w(c(FLOATS))),
        (AREA_BYTE, b(c((0, 1, 2, 3)))), (0x810841, O.w32(c((0, 1, 0x80, 0xFF, 0x01010101)))),
        (FLAG, b(c((0, 1, 0xFF)))), (COUNT, b(c((0, 1, 0xFF)))), (FLAG4, b(c((0, 1)))), (COUNT4, b(c((0, 1, 2)))),
        (SEEN, b(c((0, 1, 2, 4, 7)))), (ROOM, b(c((0, 2, 7, 3)))), (MODE, b(c((0, 4, 1)))),
        (0x810350, w(c(FLOATS + [O.fbits(552.), O.fbits(600.)]))),
        (0x810354, w(c(FLOATS))), (0x810358, w(c(FLOATS + [O.fbits(235.), O.fbits(279.), O.fbits(250.)]))),
        (0x700031D8, w(c((0, 1, 2)))), (0x700031D4, w(c((0, B + 0x400)))),
        (B + 0x402, b(c((0, 4)))), (B + 0x403, b(c((0, 6, 0x1C, 0x1E, 0x1F, 5)))),
    ]


def hook_values(rng):
    def ints(*values, n=8): return [rng.choice(values) for _ in range(n)]
    def floats(n=8): return [rng.choice(FLOATS) for _ in range(n)]
    out = {a: ints(*RESULTS) for a in (0x1B0FD0, 0x1BA1F0, 0x1B17A0, 0x1B1020, 0x1BA1C0, 0x21BE40, 0x19AA80, 0x19A570,
                                       0x21BB00, 0x21BC40, 0x1B10B0, 0x1B1EA0, 0x1FB9F0, 0x17B490, 0x1C64F0)}
    out.update({0x1AFA90: ints(0, NODE), 0x1F4F40: ints(0, NODE), 0x1B39F0: ints(0, 1, 0x7F, 0x80, 0x1FF, -1),
                0x1B1380: ints(0, 1, 0x7F, 0x80, 0x1FF, -1), 0x1B15D0: floats(), 0x11E420: floats(), 0x102738: floats()})
    return out


def cases():
    out, labels = [], set()
    base = common()
    b, h, w, f, s = O.b8, O.h16, O.w32, O.f32b, O.Scribble

    def add(label, e, patches=(), queues=None, run=MATH, args=None, beat=None):
        assert label not in labels, label
        labels.add(label)
        all_patches = base + list(patches)
        out.append((label, beat or beat_of(e), e, tuple(args or O.DEFAULT_ARGS[e]),
                    [(a, v) for a, v in all_patches if a < 0x70000000],
                    [(a, v) for a, v in all_patches if a >= 0x70000000], O.Script(queues), set(run)))

    def st(state, sub=0):
        return [(A + 4, b(state)), (A + 5, b(sub))]

    def ok(*addresses):
        """The designed cases' stubbed helpers."""
        return MATH - set(addresses)

    # ---- 001B3670: the angle, both turns, the sign of the probe's result
    for angle in (0., 2., 2.3561945, 2.3561947, 3., -1.):
        for result in (0, 1, -1):
            add(f'3670/angle/{angle}/{result}', 0x1B3670, [], {0x11E420: [O.fbits(angle)], 0x1B1380: [result],
                                                               0x1B39F0: [result]}, ok(0x11E420))
    for raw in (NAN, INF, NINF, TINY):
        add(f'3670/raw/{raw:x}', 0x1B3670, [], {0x11E420: [raw]}, ok(0x11E420))
    for result in (0, 1, 0x7F, 0x80, 0xFF, 0x100, 0x1FF, -1):
        add(f'3670/sign/{result}', 0x1B3670, [], {0x11E420: [O.fbits(3.)], 0x1B39F0: [result], 0x1B15D0: [O.fbits(7.)]},
            ok(0x11E420))
        add(f'3670/plain/{result}', 0x1B3670, [], {0x11E420: [O.fbits(1.)], 0x1B1380: [result]}, ok(0x11E420))
    add('3670/math/behind', 0x1B3670, [(A + 0xC4, f(0.))], {0x1B15D0: [O.fbits(7.)]})
    frame = [(O.STACK_TOP - 0x20, O.fw(0., 0., 1., 1.))]
    add('3670/frame', 0x1B3670, [], {0x1B2B10: [s(0, frame)], 0x1B15D0: [O.fbits(7.)]})
    add('3670/none', 0x1B3670, [], None, ())
    add('3670/nofloat', 0x1B3670, [], {0x11E420: [O.fbits(3.)]}, ())
    # ---- 001B3C30: the beam
    hits = [(0x700031D8, w(0)), (0x700031D4, w(0))]
    for free in (0, 1, -1):
        for hull in (0, 1, 2, -1):
            for wall in (0, 1, 2, -1):
                for kind in (0, 1, 2):
                    add(f'3c30/{free}/{hull}/{wall}/{kind}', 0x1B3C30, [(0x700031D8, w(kind)), (0x700031D4, w(0))],
                        {0x21BE40: [free], 0x19AA80: [hull], 0x19A570: [wall], 0x1F4F40: [0]})
    for byte2 in (3, 4, 5):
        for byte3 in (0, 5, 6, 7, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0xFF):
            for value in (5., -2., 70000.):
                add(f'3c30/target/{byte2}/{byte3:x}/{value}', 0x1B3C30,
                    [(0x700031D8, w(2)), (0x700031D4, w(B + 0x400)), (B + 0x402, b(byte2)), (B + 0x403, b(byte3))],
                    {0x19A570: [1], 0x1F4F40: [NODE]}, args=[A, B, 0x808080, O.fb(40.), O.fb(value), O.STACK_TOP])
    for node in (0, NODE):
        add(f'3c30/node/{node:x}', 0x1B3C30, hits, {0x1F4F40: [node]})
    add('3c30/scribble', 0x1B3C30, hits,
        {0x19AA80: [s(1, [(0x700031D0, w(CONTACT + 0x40)), (CONTACT + 0x64, O.fw(1., 0., 0., 3.)), (0x700031B0, O.fw(1., 2., 3., 9.))])],
         0x19A570: [s(1, [(0x700031D0, w(CONTACT + 0x80)), (CONTACT + 0xA4, O.fw(0., 0., 1., 3.)), (0x700031B0, O.fw(4., 5., 6., 9.)),
                          (0x700031D8, w(2)), (0x700031D4, w(B + 0x400)), (B + 0x402, b(4))])]})
    add('3c30/stub', 0x1B3C30, hits, {0x19AA80: [1], 0x1F4F40: [NODE]}, ())
    add('3c30/stub/wall', 0x1B3C30, hits, {0x19A570: [1], 0x1F4F40: [NODE]}, ())
    # ---- 001BD460: both floats around each comparison
    for f14 in (0., .1, .2, .3, 2., 2.2, 2.19, 3., -1.):
        for f10 in (0., .1, .2, .3, 1., -1.):
            add(f'd460/{f14}/{f10}', 0x1BD460, [(A + 0x14, f(f14)), (A + 0x10, f(f10))])
    for raw in (NAN, INF, NINF, TINY, 0x80000001):
        add(f'd460/raw/{raw:x}', 0x1BD460, [(A + 0x14, w(raw)), (A + 0x10, w(raw))])
        add(f'd460/raw10/{raw:x}', 0x1BD460, [(A + 0x14, f(1.)), (A + 0x10, w(raw))])
    # ---- 001C82A0: the bones, their parents
    def bones(n, parents):
        p = [(A + 0xC, b(n)), (A + 0x60, O.fw(1., 1., 1., 1.)), (A + 0xC0, O.fw(.1, .2, .3, 0.))]
        for i in range(n):
            bone = AUX + 0x1400 + i * 0x200
            p += [(A + 0x110 + i * 4, w(bone)), (bone + 0x64, h(parents[i])), (bone + 0x70, O.fw(1., 2., 3., 1.)),
                  (bone + 0x7C, O.fw(.5, .25, .125)), (bone + 0x88, h(0x400 + i)), (bone + 0x8A, h(0xFC00)), (bone + 0x8C, h(0x7FFF))]
            p += [(B + i * 0x40, O.fw(1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.))]
        return p
    for n, parents in ((0, ()), (1, (0xFFFF,)), (2, (0xFFFF, 0)), (3, (0xFFFF, 0, 1)), (2, (0, 0xFFFF))):
        add(f'82a0/{n}/{parents}', 0x1C82A0, bones(n, parents), None, ok(0x1026D0))
        add(f'82a0/stub/{n}/{parents}', 0x1C82A0, bones(n, parents), None, ())
    add('82a0/live', 0x1C82A0, bones(2, (0xFFFF, 0)))
    add('82a0/scribble', 0x1C82A0, bones(2, (0xFFFF, 0)) ,
        {0x1C94B0: [s(0, [(A + 0xC, b(1)), (AUX + 0x1464, h(0))])]}, ok(0x1026D0))
    # ---- 00158430: the seal
    for area in (0, 1, 3):
        for bits in (0, 1, 2, 0x80, 0xFF):
            for bit in (0, 1, 7, 8, 9, 0x100, 0x101):
                for loaded in (0, 1):
                    add(f'8430/0/{area}/{bits:x}/{bit:x}/{loaded}', 0x158430,
                        [(AREA_BYTE, b(area)), (0x810841 + area, b(bits)), (A + 0x2E, h(bit))], {0x1B0FD0: [loaded]})
    for hit in (0, 1, 0x8000):
        for bit in (0, 3, 7, 8):
            for mask in (0, 0xF0, 4):
                add(f'8430/1/{hit:x}/{bit}/{mask:x}', 0x158430,
                    st(1) + [(A + 0x36, h(hit)), (A + 0x2E, h(bit)), (AREA_BYTE, b(1)), (0x810842, b(mask)), (0x810845, b(mask))])
    for state in (2, 3, 4, 0xFF):
        add(f'8430/state/{state}', 0x158430, st(state))
    # ---- AREA03 0x8235A0
    for flag in (0, 1, 0xFF, 2):
        for loaded in (0, 1):
            add(f'35a0/0/{flag:x}/{loaded}', 0x8235A0, [(FLAG, b(flag))], {0x1B0FD0: [loaded]})
        for use in (0, 4, 3, 0xFF):
            add(f'35a0/1/{flag:x}/{use:x}', 0x8235A0, st(1) + [(FLAG, b(flag)), (A + 0xB, b(use))])
    for count in (0, 1, 0xFF):
        for done in (0, 1, 2, -1):
            add(f'35a0/1/1/{count:x}/{done}', 0x8235A0, st(1, 1) + [(COUNT, b(count)), (A + 0xB, b(4))], {0x1BA1F0: [done]})
    for state, sub in ((1, 2), (1, 3), (1, 0xFF), (2, 0), (3, 0), (4, 0), (0xFF, 0)):
        add(f'35a0/state/{state}/{sub}', 0x8235A0, st(state, sub))
    add('35a0/scribble', 0x8235A0, st(1) + [(FLAG, b(1)), (A + 0xB, b(4))],
        {0x1BA1A0: [s(0, [(FLAG, b(0xFF))]), 0]})
    # ---- AREA03 0x823810
    for loaded in (0, 1, -1):
        add(f'3810/0/{loaded}', 0x823810, [(A + 0xD, b(0x80))], {0x1B1020: [loaded]})
    for sub in (0, 1, 2, 0xFF):
        for use in (0, 4, 3):
            for flag in (0, 1, 0xFF):
                for count in (0, 1, 0xFF):
                    for moved in (0, 1, -1):
                        add(f'3810/1/{sub}/{use}/{flag:x}/{count:x}/{moved}', 0x823810,
                            st(1, sub) + [(A + 0xB, b(use)), (FLAG, b(flag)), (COUNT, b(count))], {0x1B17A0: [moved]})
    for state in (2, 3, 4, 0xFF):
        add(f'3810/state/{state}', 0x823810, st(state) + [(A + 0x9A, b(0x80))])
    # ---- AREA03 0x825200
    for node in (0, NODE):
        add(f'5200/{node:x}', 0x825200, [(A, O.fw(*range(16))), (A + 0x30, O.fw(7., 8., 9., 1.))], {0x1AFA90: [node]})
    add('5200/stub', 0x825200, [], {0x1AFA90: [NODE]}, ())
    # ---- AREA04 0x824EF0, with 0x825040 and 0x8251C0
    for done in (0, 1, -1):
        for flag in (0, 0x80):
            add(f'4ef0/0/{done}/{flag}', 0x824EF0, [(A + 0xD, b(flag))], {0x1BA1C0: [done]})
    for state in (2, 3, 4, 0xFF):
        add(f'4ef0/state/{state}', 0x824EF0, st(state))
    none = (COUNT4, b(0))   # the recording's counter is not 0 at the start of these
    add('4ef0/1/2', 0x824EF0, st(1) + [(COUNT4, b(2))])
    add('4ef0/1/ff', 0x824EF0, st(1) + [(COUNT4, b(0xFF))])
    for inside in (0, 1, 2, -1):
        for y in (24., 25., 26.):
            add(f'5040/0/{inside}/{y}', 0x824EF0, st(1) + [none, (0x810354, f(y)), (A + 0xD, b(0x81))], {0x1B1EA0: [inside]})
    for raw in (NAN, INF, NINF):
        add(f'5040/0/raw/{raw:x}', 0x824EF0, st(1) + [none, (0x810354, w(raw))], {0x1B1EA0: [1]})
    for done in (0, 1, -1):
        for step in (0, 5, 0x8001, -1, 0x12345):
            add(f'5040/1/{done}/{step}', 0x824EF0, st(1, 1) + [none, (A + 0xD, b(0x81))], {0x1BA1F0: [done], 0x1C64F0: [step]})
    for sub in (2, 3, 0xFF):
        add(f'5040/{sub}', 0x824EF0, st(1, sub) + [none])
    for use in (0, 4, 3):
        add(f'51c0/0/{use}', 0x824EF0, st(1) + [(COUNT4, b(1)), (A + 0xB, b(use))])
    for done in (0, 1, -1):
        add(f'51c0/1/{done}', 0x824EF0, st(1, 1) + [(COUNT4, b(1)), (A + 0xB, b(0xFF))], {0x1BA1F0: [done]})
    for sub in (2, 3, 0xFF):
        add(f'51c0/{sub}', 0x824EF0, st(1, sub) + [(COUNT4, b(1))])
    add('5040/scribble', 0x824EF0, st(1) + [none], {0x1B1EA0: [s(1, [(0x810354, f(1.))])], 0x1BA1A0: [s(0, [(COUNT4, b(1))])]})
    # ---- AREA04 0x825310, with 0x825280
    add('5310/0', 0x825310)
    for state in (2, 3, 4, 0xFF):
        add(f'5310/state/{state}', 0x825310, st(state))
    for done in (0, 1, -1):
        add(f'5310/1/{done}', 0x825310, st(1, 1), {0x1BA1F0: [done]})
    for sub in (2, 3, 0xFF):
        add(f'5310/sub/{sub}', 0x825310, st(1, sub))
    for flag in (0, 1, -1):
        for room in (0, 2, 7, 1):
            for mode in (0, 4):
                for seen in (0, 1, 2, 3, 4, 5, 7, 0xF8):
                    add(f'5310/arrive/{flag}/{room}/{mode}/{seen}', 0x825310,
                        st(1) + [(ROOM, b(room)), (MODE, b(mode)), (SEEN, b(seen))], {0x1BA1C0: [flag]})
    for x, z in ((551., 250.), (552., 250.), (552., 279.), (552., 279.1), (552., 235.), (552., 234.9), (600., 250.)):
        for seen in (0, 4):
            add(f'5280/{x}/{z}/{seen}', 0x825310, st(1) + [(ROOM, b(7)), (SEEN, b(seen)), (0x810350, f(x)), (0x810358, f(z))],
                {0x1BA1C0: [1]})
    for raw in (NAN, INF, NINF):
        add(f'5280/raw/{raw:x}', 0x825310, st(1) + [(ROOM, b(7)), (0x810350, w(raw)), (0x810358, w(raw))], {0x1BA1C0: [1]})
        add(f'5280/rawz/{raw:x}', 0x825310, st(1) + [(ROOM, b(7)), (0x810350, f(600.)), (0x810358, w(raw))], {0x1BA1C0: [1]})
    # ---- 0017C370
    for mode in (0, 5, 6, 7, 8):
        for kind in (0, 3, 2):
            for side in (0, 1, 0x80, 0xFF):
                add(f'c370/{mode}/{kind}/{side}', 0x17C370, [(A + 0x1F0, b(mode)), (A + 0x1F1, b(kind)), (A + 0x235, b(side))],
                    {0x17B490: [9, 0x80]})
    for clip in (0, 1, -1, 0x12345):
        add(f'c370/clip/{clip}', 0x17C370, [(A + 0x1F0, b(6))], {0x17B490: [clip]})
    for yaw in (0., 3.1415927, -3.1415927, 4., 7.):
        add(f'c370/yaw/{yaw}', 0x17C370, [(A + 0x1F0, b(7)), (A + 0xC4, f(yaw))])
    # ---- 0021BED0: every reason, in order
    live = [(A, b(1)), (A + 4, b(1)), (A + 0xA, b(1)), (A + 0x220, f(5.)), (MODE, b(0))]
    add('bed0/free', 0x21BED0, live)
    for label, patch in (('mode', (MODE, b(1))), ('hp0', (A + 0x220, f(0.))), ('hp', (A + 0x220, f(-1.))), ('nan', (A + 0x220, w(NAN))),
                         ('b0', (A, b(2))), ('b4', (A + 4, b(2))), ('236', (A + 0x236, b(1))), ('a', (A + 0xA, b(0))),
                         ('20e', (A + 0x20E, h(1))), ('20e8', (A + 0x20E, h(0x8000)))):
        add(f'bed0/{label}', 0x21BED0, live + [patch])
    for first, second in ((1, 0), (0, 1), (0, -1), (-1, 0), (0x100, 0)):
        add(f'bed0/hooks/{first}/{second}', 0x21BED0, live, {0x21BB00: [first], 0x21BC40: [second]})
    # ---- 0021BF90 / 0021C040
    for kind in range(10):
        for start in (0, 0x81):
            add(f'bf90/{kind}/{start}', 0x21BF90, [(B + 3, b(kind)), (A, b(start)), (A + 0xD, b(0xEE)), (A + 0x25F, b(0xEE)), (A + 0xF, b(0xEE))])
        add(f'c040/{kind}', 0x21C040, [(B + 3, b(kind)), (A + 0x1F0, b(0x3C)), (A + 0xD, b(0xEE)), (A + 0xF, b(0xEE))])
        add(f'c040/no/{kind}', 0x21C040, [(B + 3, b(kind)), (A + 0x1F0, b(0x3D))])
    add('bf90/0xFF', 0x21BF90, [(B + 3, b(0xFF))])
    add('c040/0xFF', 0x21C040, [(B + 3, b(0xFF)), (A + 0x1F0, b(0x3C))])
    # ---- a seeded random sample over everything, on the entry's own image
    rng = random.Random(0x16B007)
    for entry in FUNCS:
        for n in range(O.reference_mode.pick(300, 16)):
            run = MATH if n % 3 else MATH - {0x11E420, 0x102738}
            add(f'random/{entry:06X}/{n}', entry, fields(rng), hook_values(rng), run)
        if entry < O.ARENA:
            for n in range(O.reference_mode.pick(40, 4)):
                add(f'other/{entry:06X}/{n}', entry, fields(rng), hook_values(rng), beat=BEAT4)
    return out


def header_checks():
    header = (ROOT / 'src/game/em_level16_boot.h').read_text()
    block = header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found = re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);', block)
    assert [n for n, _ in found] == [h[0] for h in HOOKS]
    types = {U: 'uint32_t', I: 'int32_t', F: 'float', Q: 'uint64_t'}
    for (name, params), (expected, address, args, result) in zip(found, HOOKS):
        actual = [re.sub(r'\s*\b\w+$', '', p.strip()).replace(' ', '') for p in params.split(',')[1:]]
        assert actual == [types[k] for k, _ in args] + ([types[result] + '*'] if result else [])
    internal = (ROOT / 'src/game/em_level16_boot_internal.h').read_text()
    for name, address, _, _ in HOOKS:
        bodies = re.findall(r'static inline int l16b_c_%s\(.*?\n}\n' % name[2:], internal, re.S)
        assert len(bodies) == 1 and f'o->h->{name}(' in bodies[0] and f'0x{address:08X}u' in bodies[0]
    for entry, (symbol, _, kinds, result) in FUNCS.items():
        assert re.search(r'^int %s\(.*\);$' % symbol, header, re.M), symbol


def entry_guards():
    touched = []

    def refuse(*_):
        touched.append(1)
        return -1
    keep = [O.BYTES_FN(refuse), O.CALLBACK_FN(refuse)]
    fields_ = {'ctx': None, 'bytes': keep[0], 'w_callback': keep[1]}
    for name, _, args, result in HOOKS:
        fields_[name] = O.hook_proto(args, result)(refuse)
        keep.append(fields_[name])
    hooks = O.Hooks(**fields_)
    for entry, (symbol, _, _, kind) in FUNCS.items():
        for table, fault, with_result in ((C.byref(hooks), O.Fault(0x1234, 7), True),
                                          (None, O.Fault(), True), (C.byref(hooks), None, True),
                                          (C.byref(hooks), O.Fault(), False)):
            if not with_result and kind is None:
                continue
            out = C.c_int32(0x5A5A5A5A)
            touched.clear()
            args = [table] + O.native_args(entry, O.DEFAULT_ARGS[entry])
            if kind is not None:
                args.append(C.byref(out) if with_result else None)
            rc = getattr(O.NATIVE, symbol)(*args, C.byref(fault) if fault is not None else None)
            assert rc == -1 and not touched and out.value == 0x5A5A5A5A
            if fault is not None:
                assert (fault.address, fault.code) == ((0x1234, 7) if table is not None and with_result else (0, 0))


def trace(prefix, selected):
    for case in selected:
        if not case[0].startswith(prefix):
            continue
        label, beat, entry, args, patches, spad, script, run = case
        ram0, spad0 = O.CAPTURES[beat]
        ram, sp = O.patched(ram0, patches), O.patched(spad0, spad)
        oracle = O.Oracle(ram, sp, entry, (args, O.callbacks_of(entry, args, ram)), O.copy_script(script), run, set())
        print('==', label)
        for t in oracle.trace:
            print(' ', t[0], *(hex(x) if isinstance(x, int) else x for x in t[1:]))


def callee_checks():
    """O.callee_checks over the entries and the inlined pieces."""
    keep = O.FUNCS
    O.FUNCS = {**FUNCS, **{p: (None, size, [], None) for p, (size, _) in PIECES.items()}}
    try:
        return O.callee_checks()
    finally:
        O.FUNCS = keep


def judge(results, areas, callees):
    """Coverage, inventory and the contract of one pass. Returns the failures."""
    only = os.environ.get('EM_LEVEL16_BOOT_ONLY')
    failures = [(name, errors) for name, _, errors, _, _, _ in results if errors]
    seen = set().union(*(r[3] for r in results)) if results else set()
    for entry, size in areas:
        words = O.reachable_words(O.image_for(entry), entry, size)
        missing = words - seen - DEAD
        print(f'{entry:08X}: {len(words & seen)}/{len(words - DEAD)} reachable words')
        if missing and not only:
            failures.append(('coverage', [hex(entry), [hex(m) for m in sorted(missing)]]))
    if not only:
        assert not DEAD & seen, 'a word called dead ran'
        failures += [('callee inventory', p) for p in callees()]
        if not failures:
            sites, missing = O.hook_sites(results)
            if missing:
                failures.append(('unreached hooks', missing))
            for problems, runs in O.reference_mode.parallel_map(O.hook_contract_site, sites):
                if problems:
                    failures.append(('fail-stop contract', problems))
    return failures


def level14_pass():
    """001ED450 through the level-14 module's own hook table."""
    O.HOOKS, O.Hooks, O.SOURCES = LEVEL14
    entry = 0x1ED450
    O.FUNCS = {entry: ('em_level14_port_001ED450', 840, [U, U], None)}
    O.OUT = ROOT / 'build/levels/level16_boot14'
    O.BY_ADDRESS = {a: (n, args, res) for n, a, args, res in O.HOOKS}
    O.BY_NAME = {n: a for n, a, _, _ in O.HOOKS}
    O.HOOK_NAMES = ['w_001CFB50', 'w_001CFBE0']
    O.SELF_ENTRIES = set()
    O.DEFAULT_ARGS = {entry: [FREE + 0x100, FREE + 0x200]}
    O.image_for = lambda _: O.CAPTURES[BEAT3][0]
    problems, _ = O.header_checks()
    assert not problems, problems
    O.NATIVE = O.build(os.environ.get('EM_LEVEL16_BOOT_14_SOURCE'))
    out = []

    def add(label, r, f8, s54=2.5):
        patches = [(0x275C34, O.w32(AUX)), (AUX + 4, O.w32(r)), (AUX + 8, f8), (AUX + 0x54, O.f32b(s54))]
        out.append((label, BEAT3, entry, tuple(O.DEFAULT_ARGS[entry]), patches, [], O.Script(), set()))
    for r, f8 in ((0, .5), (0x12345678, .021), (-1, .02), (0x7FFF0000, .01), (0xFFFF0000, -1.)):
        add(f'd450/{r & O.MASK:x}/{f8}', r, O.f32b(f8))
    for v in O.around(0.02):
        add(f'd450/bound/{v:x}', 7, O.bits32(v))
    for v in (NAN, INF, NINF, TINY):
        add(f'd450/raw/{v:x}', 3, O.w32(v))
    out.append(('d450/live', BEAT3, entry, tuple(O.DEFAULT_ARGS[entry]), [], [], O.Script(), set()))
    O.CASES[:] = out
    results = O.run_cases(out)
    return judge(results, [(entry, 840)], O.callee_checks), len(out)


def main():
    configure()
    O.NATIVE = O.build(os.environ.get('EM_LEVEL16_BOOT_SOURCE'))
    header_checks()
    entry_guards()
    selected = cases()
    only = os.environ.get('EM_LEVEL16_BOOT_ONLY')
    if only:
        selected = [c for c in selected if c[0].startswith(only)]
    O.CASES[:] = selected
    assert len({c[0] for c in selected}) == len(selected)
    if os.environ.get('EM_LEVEL16_BOOT_TRACE'):
        trace(os.environ['EM_LEVEL16_BOOT_TRACE'], selected)
        return
    results = O.run_cases(selected)
    areas = [(e, v[1]) for e, v in FUNCS.items()] + [(p, v[0]) for p, v in PIECES.items()]
    failures = judge(results, areas, callee_checks)
    total = len(selected)
    if not only:
        more, count = level14_pass()
        failures += more
        total += count
    for label, errors in failures[:int(os.environ.get('EM_LEVEL16_BOOT_SHOW', '8'))]:
        print('FAIL', label, errors)
    assert not failures, f'{len(failures)} failures'
    O.reference_mode.banner(f'{total} cases')
    print(f'PASS {total} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__ == '__main__':
    main()
