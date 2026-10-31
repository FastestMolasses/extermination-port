#!/usr/bin/env python3
"""The AREA03 creature's remaining states (src/game/em_level16_creature.c).

Unwired. Every comparison of the level-14 oracle stays on: the entries run as
original instructions from the user's ELF over a fork recording of AREA03.
A default run is a designed set plus a seeded random sample; EM_TEST_FULL=1
widens the random sample and sweeps every failure site. Diagnostic subsets
(EM_LEVEL16_CREATURE_ONLY) grant no proof.
"""
import ctypes as C
import os
import random
import re
from pathlib import Path
import test_level14_port_reference as O
import reference_root as refs
ROOT = Path(__file__).resolve().parents[1]
U, I, F, Q, S = O.U, O.I, O.F, O.Q, O.S
FUNCS = {
    0x0013D980: ('em_level16_creature_0013D980', 0x1F0, [U, U], None),
    0x0013DB70: ('em_level16_creature_0013DB70', 0xF0, [U, U], None),
    0x0013DC60: ('em_level16_creature_0013DC60', 0xE0, [U, U], None),
    0x0013DD40: ('em_level16_creature_0013DD40', 0x990, [U, U, S], None),
    0x0013E6D0: ('em_level16_creature_0013E6D0', 0x4C0, [U, U, S], None),
    0x0013EB90: ('em_level16_creature_0013EB90', 0x220, [U, U], None),
    0x0013EDB0: ('em_level16_creature_0013EDB0', 0x490, [U, U], None),
    0x0013F240: ('em_level16_creature_0013F240', 0x528, [U, U], None),
    0x0013F770: ('em_level16_creature_0013F770', 0xCD0, [U, U], None),
    0x00140440: ('em_level16_creature_00140440', 0x250, [U, U], None),
    0x00140760: ('em_level16_creature_00140760', 0x170, [U, U], None),
    0x001408D0: ('em_level16_creature_001408D0', 0x1C0, [U, U], None),
    0x00140A90: ('em_level16_creature_00140A90', 0x170, [U, U], None),
    0x001416D0: ('em_level16_creature_001416D0', 0x220, [U, U, S], I),
}
OVERLAY_OF = {}
OVERLAY_FILES = {4: ROOT.parent / 'Extermination/extract/OVERLAY/AREA03.BIN'}
HOOKS = [
    ('w_001026A0', 0x001026A0, [('u', 4), ('u', 5), ('u', 6)], None),
    ('w_001028B8', 0x001028B8, [('u', 4), ('u', 5), ('u', 6)], None),
    ('w_00102760', 0x00102760, [('u', 4), ('u', 5)], None),
    ('w_00102948', 0x00102948, [('u', 4), ('u', 5)], None),
    ('w_00102958', 0x00102958, [('u', 4), ('u', 5)], None),
    ('w_001029C0', 0x001029C0, [('u', 4)], None),
    ('w_00102B08', 0x00102B08, [('u', 4), ('u', 5), ('f', 12)], None),
    ('w_00102BB0', 0x00102BB0, [('u', 4), ('u', 5), ('f', 12)], None),
    ('w_0011DF78', 0x0011DF78, [('f', 12)], 'f'),
    ('w_0011E620', 0x0011E620, [('f', 12), ('f', 13)], 'f'),
    ('w_00122BB8', 0x00122BB8, [], 'i'),
    ('w_001418F0', 0x001418F0, [('u', 4), ('u', 5)], None),
    ('w_0019A6F0', 0x0019A6F0, [('u', 4), ('u', 5), ('u', 6), ('i', 7), ('i', 8)], 'i'),
    ('w_001A7B80', 0x001A7B80, [('u', 4)], 'i'),
    ('w_001B1190', 0x001B1190, [('i', 4)], None),
    ('w_001B1240', 0x001B1240, [('u', 4), ('f', 12), ('f', 13)], 'f'),
    ('w_001B12B0', 0x001B12B0, [('f', 12), ('f', 13), ('f', 14)], 'f'),
    ('w_001B13F0', 0x001B13F0, [('u', 4), ('u', 5), ('f', 12)], 'i'),
    ('w_001B1470', 0x001B1470, [('f', 12)], 'f'),
    ('w_001B1560', 0x001B1560, [('u', 4), ('u', 5), ('f', 12)], 'i'),
    ('w_001B15D0', 0x001B15D0, [('u', 4), ('u', 5)], 'f'),
    ('w_001B2B10', 0x001B2B10, [('u', 4), ('u', 5), ('u', 6)], None),
    ('w_001B2BF0', 0x001B2BF0, [('u', 4), ('u', 5), ('u', 6), ('f', 12)], 'i'),
    ('w_001B3250', 0x001B3250, [('u', 4), ('u', 5), ('f', 12)], 'i'),
    ('w_001B3390', 0x001B3390, [('u', 4), ('u', 5), ('u', 6), ('f', 12)], 'i'),
    ('w_001B3670', 0x001B3670, [('u', 4), ('u', 5), ('u', 6)], 'i'),
    ('w_001B37D0', 0x001B37D0, [('u', 4), ('f', 12), ('f', 13)], 'f'),
    ('w_001B55E0', 0x001B55E0, [('u', 4), ('i', 5)], None),
    ('w_001C63E0', 0x001C63E0, [('u', 4), ('i', 5)], None),
    ('w_001C67E0', 0x001C67E0, [('u', 4), ('i', 5), ('f', 12), ('f', 13)], None),
    ('w_001E8B90', 0x001E8B90, [('u', 4), ('f', 12)], None),
    ('w_001EFD90', 0x001EFD90, [('i', 4), ('u', 5), ('u', 6)], None),
    ('w_001EFE00', 0x001EFE00, [('i', 4), ('u', 5)], 'i'),
    ('w_001EFFD0', 0x001EFFD0, [('i', 4), ('u', 5), ('u', 6), ('i', 7), ('f', 12)], None),
    ('w_001FBD50', 0x001FBD50, [('u', 4), ('i', 5), ('i', 6), ('f', 12)], None),
    ('w_0021BE40', 0x0021BE40, [('u', 4), ('u', 5)], 'i'),
    ('w_0021BED0', 0x0021BED0, [('u', 4)], 'i'),
    ('w_0021BF90', 0x0021BF90, [('u', 4), ('u', 5)], None),
    ('w_0021C040', 0x0021C040, [('u', 4), ('u', 5)], None),
]
BEAT = 'a03b_01_duct_west'
BEATS = (BEAT,)
FREE = 0x1E61000
A, B = FREE + 0x1000, FREE + 0x2000
TARGET, BONES = B + 0x400, FREE + 0x3000
FAR = 0x70003B68, 0x70003B8A
# original vector helpers: they run as original code, their writes replayed
PURE = (0x1026A0, 0x1028B8, 0x102948, 0x102958, 0x1029C0, 0x102760, 0x102B08, 0x102BB0, 0x11DF78)
RANDOM_CASES = O.reference_mode.pick(400, 20)   # per entry and sub-state
# the sub-states of each entry's switch, one past the last (the no-op default)
STATES = {0x13D980: 3, 0x13DB70: 3, 0x13DC60: 3, 0x13DD40: 4, 0x13E6D0: 5, 0x13EB90: 4, 0x13EDB0: 4, 0x13F240: 5,
          0x13F770: 10, 0x140440: 5, 0x140760: 3, 0x1408D0: 4, 0x140A90: 4}


def configure():
    O.FREE = FREE
    O.FUNCS, O.HOOKS = FUNCS, HOOKS
    O.OVERLAY_OF, O.OVERLAY_FILES = OVERLAY_OF, OVERLAY_FILES
    O.OUT = ROOT / 'build/levels/level16_creature'
    O.SOURCES = ('em_level16_creature.c',)
    O.BEATS = BEATS
    O.BEAT_DIRS = {BEAT: refs.capture('levels-recordings-20261030-1/s87/route_a03b/' + BEAT)}
    O.RESIDENT = {BEAT: 4}
    O.BY_ADDRESS = {a: (n, args, res) for n, a, args, res in HOOKS}
    O.BY_NAME = {n: a for n, a, _, _ in HOOKS}
    O.HOOK_NAMES = [h[0] for h in HOOKS]
    O.Hooks = type('Hooks', (C.Structure,), {'_fields_':
        [('ctx', C.c_void_p), ('bytes', O.BYTES_FN)] +
        [(name, O.hook_proto(args, res)) for name, _, args, res in HOOKS] + [('w_callback', O.CALLBACK_FN)]})
    O.SELF_ENTRIES = set()
    O.HEAVY = set()
    O.RETURN_SITES = O.RETURN_SITES_FULL = (0x1416D0,)
    O.CONTRACT_UNREACHABLE = {}
    O.LOCAL_STORES = set()
    O.DEFAULT_ARGS = {e: [A, B] + ([O.STACK_TOP] if len(k) == 3 else []) for e, (_, _, k, _) in FUNCS.items()}
    O.image_for = lambda _: O.CAPTURES[BEAT][0]
    O.CAPTURES.clear()
    O.CAPTURES.update(O.load_captures(O.read_elf()))
    # The grab's nine-state dispatcher table (the translation is a switch).
    table = 0x26D240
    elf = O.read_elf()
    expected = elf[0x300 + table - 0x100000:0x300 + table - 0x100000 + 36]
    for ram, _ in O.CAPTURES.values():
        assert ram[table:table + 36] == expected
    O.JUMP_TABLES = {0x13F7A8: (table, 9)}
    O.TABLE_BYTES = frozenset(range(table, table + 36))


NAN, INF, NINF, TINY = 0x7FC00000, 0x7F800000, 0xFF800000, 1
FLOATS = [O.fbits(v) for v in (0., 1., -1., .5, .6, .61, 1.1, 1.2, 16., 15.9, 16.1, 20., 20.01, -.3, -.6, -1.2)] + \
         [0x80000000, NAN, INF, NINF, TINY, 0x80000001]
ANGLES = [O.fbits(v) for v in (0., .3, -.3, .39, .4, 1.5, -1.5, 3.1, -3.1, 3.2, 7.)] + [NAN, INF, TINY]
FRAMES = [O.fbits(v) for v in (77., 64., 52., 19., 62., 16., 15.9, 16.1, 0., 1.)] + [NAN]
DISTANCES = [O.fbits(v) for v in (0., 12., 20., 20.01, 25., 30., 100., 31., 5.)] + [NAN, INF]
WORDS = (0, 1, 2, 0x1000, 9, 0x1009, 0x28, 0xB3, 0xB4, 0x81, 0x82, 0xFFFFFFFF)
FLAGS = (0, 0x1000, 0x1000, 0x1000, 9, 0x1009, 0x2000, 0x10000)
BYTES = (0, 1, 2, 3, 0x7F, 0x80, 0x81, 0xFF)


def hook_values(rng):
    """A per-callee result queue, rich enough that most branches go both ways."""
    def ints(*values, n=8): return [rng.choice(values) for _ in range(n)]
    def floats(pool, n=8): return [rng.choice(pool) for _ in range(n)]
    return {
        0x122BB8: [rng.getrandbits(32) for _ in range(8)],
        0x1B2BF0: ints(0, 1, 2, 3, 4, 5, 6, 7), 0x1B3670: ints(0, 1, 2, 0x1FF, -1),
        0x1B37D0: floats(ANGLES), 0x1B1240: floats(ANGLES), 0x1B12B0: floats(ANGLES),
        0x1B1470: floats(ANGLES), 0x11DF78: floats(ANGLES), 0x11E620: floats(ANGLES), 0x1B15D0: floats(FLOATS),
        0x1B1560: ints(0, 1, 1, 0x100, -1), 0x1B13F0: ints(0, 1, 0, 0x100, -1), 0x1B3250: ints(0, 1, 0, 0x100, -1),
        0x1B3390: ints(0, 1, 0x100, -1), 0x19A6F0: ints(0, 1, 0x100, -1), 0x1A7B80: ints(0, 1, 1, 0x100, -1),
        0x21BE40: ints(0, 0, 1, 0x100, -1), 0x21BED0: ints(0, 0, 1, 0x100, -1), 0x1EFE00: ints(0, 1, 0x7A0000, -1),
    }


def fields(rng):
    """The fields the workers branch on, each a random interesting value."""
    b, h, w = O.b8, O.h16, O.w32
    c = rng.choice
    return [
        (A, b(c((0, 1, 3)))), (A + 0xA, b(c((0, 0, 1, 2, 3)))), (A + 0xD, b(c((0, 0x80, 0x7F, 0xFF)))),
        (A + 0x2C, h(c((0, 2, 3, 0x7FFF, 0x8000, 0x8002, 0xFFFF)))), (A + 0x34, h(c((0, 1, 0x64)))),
        (A + 0x36, h(c((0, 0x8000, 0x7FFF)))), (A + 0x3C, w(c(FRAMES))), (A + 0x5E, b(c((0, 1)))),
        (A + 0x60, w(c(FLOATS))), (A + 0x70, w(c(ANGLES))), (A + 0x78, w(c(ANGLES))), (A + 0x9A, b(c((0, 5, 0xFF)))),
        (A + 0xB0, O.fw(2., 3., 4., 1.)), (A + 0xB4, w(c(FLOATS))), (A + 0xC4, w(c(ANGLES))),
        (B, O.fw(5., 6., 7., 1.)), (B + 0x20, w(c(WORDS))), (B + 0x24, w(c(WORDS))), (B + 0x28, w(c(WORDS))),
        (B + 0x30, w(c(FLAGS))), (B + 0x44, w(c(ANGLES))), (B + 0x48, w(c(FLOATS))), (B + 0x4C, w(c(FLOATS))),
        (B + 0x54, w(c(ANGLES))), (B + 0x5C, w(c(DISTANCES))), (B + 0x60, w(c(FLOATS))),
        (B + 0x70, h(c((0, 0, 1, 0x8000)))), (B + 0x72, h(c((0, 1, 0xFFFF)))), (B + 0x80, b(c(BYTES))),
        (B + 0x82, b(c((0, 1, 2, 3, 0x7F, 0x80)))), (B + 0x83, b(c((0, 1, 2, 2, 0xFF)))), (B + 0x84, b(c((0, 1, 0x80)))),
        (B + 0x86, b(c(BYTES))), (B + 0x89, b(c((0, 1, 0x80)))), (B + 0x8C, b(c((0, 1, 0x80)))),
        (0x81050C, b(c((0, 1, 2, 3)))), (0x81070A, b(c((0, 1, 0x80)))), (0x8106BD, b(c((0, 1, 2)))),
        (0x8104D0, w(c(DISTANCES))), (0x810354, w(c(FLOATS))), (0x810360, O.fw(9., 8., 7., 1.)),
        (FAR[0], w(c((0, 0x40, 0x3F, 0x41, 0x7FFFFFFF)))), (FAR[1], h(c((0, 1, 0x40, 0xFFFF, 0xFFC1)))),
        (A + 0x14C, w(BONES)), (0x700031D0, w(TARGET)), (TARGET + 0x24, O.fw(1., 2., 3., 4.)),
        (0x700031B0, O.fw(5., 6., 7., 8.)), (0x700038D0, w(c(FLOATS))), (BONES + 0x90, O.fw(*range(1, 17))),
    ]


def cases():
    out = []
    b = O.b8

    def add(label, e, patches=(), queues=None, run=PURE):
        all_patches = [(A, bytes(0x300)), (B, bytes(0x800))] + list(patches)
        out.append((label, BEAT, e, tuple(O.DEFAULT_ARGS[e]),
                    [(a, v) for a, v in all_patches if a < 0x70000000],
                    [(a, v) for a, v in all_patches if a >= 0x70000000], O.Script(queues), set(run)))

    rng = random.Random(0x13D2D0)
    for entry, states in STATES.items():
        for state in range(states):
            for n in range(RANDOM_CASES):
                run = PURE if n % 3 else ()   # every third: all floats scripted
                add(f'{entry:06X}/{state}/{n}', entry, fields(rng) + [(A + 6, b(state))], hook_values(rng), run)
    for n in range(O.reference_mode.pick(200, 24)):
        add(f'1416D0/{n}', 0x1416D0, fields(rng), hook_values(rng))
    designed(add)
    return out


def designed(add):
    """The branches a random start seldom reaches, and the boundaries."""
    b, h, w, f = O.b8, O.h16, O.w32, O.f32b
    done = (B + 0x30, w(0x1000))

    def st(n):
        return (A + 6, b(n))
    # 0013D980: the frames that cue, the leave conditions, the repeat count
    for frame in (77., 64., 52., 19., 18.):
        add(f'd980/frame/{frame}', 0x13D980, [st(1), (A + 0x3C, f(frame))])
        add(f'd980/moving/{frame}', 0x13D980, [st(1), (A + 0x2C, h(0x8000)), (A + 0x3C, f(frame))])
    for count in (0, 1, 2):
        add(f'd980/count/{count}', 0x13D980, [st(1), done, (B + 0x20, w(count))])
    for alert, level in ((0, 3), (1, 1), (1, 2), (0x8000, 3)):
        add(f'd980/leave/{alert}/{level}', 0x13D980, [st(1), (B + 0x70, h(alert)), (0x81050C, b(level))])
    add('d980/hit', 0x13D980, [st(1), (A + 0xA, b(3))])
    for alert in (0, 1):
        for value in (0, 15, 0xF0000):
            # a signed table entry, so the value is taken with its sign
            add(f'd980/start/{alert}/{value}', 0x13D980, [(B + 0x70, h(alert)), (0x2458EF, b(0x80))], {0x122BB8: [value]})
    # 0013DC60
    add('dc60/cue', 0x13DC60, [st(1), (A + 0x3C, f(62.))])
    add('dc60/moving', 0x13DC60, [st(1), (A + 0x3C, f(62.)), (A + 0x2C, h(0x8000)), done])
    # 0013DD40 sub-state 0: the masked clip, signed
    for clip in (2, 0x8002, 0x7FFF, 3, 0xFFFF):
        add(f'dd40/clip/{clip:x}', 0x13DD40, [(A + 0x2C, h(clip))], {0x122BB8: [0x12345678, 0x7FFFFFFF]})
    # 001416D0's outcomes through both callers
    near = [(A + 0xB4, f(0.)), (0x700038D0, f(1.))]   # flags bit 0 with the floor near: returns 0
    aligned = {0x1B2BF0: [0], 0x1B1470: [O.fbits(.1), O.fbits(.2)], 0x11DF78: [O.fbits(.1)],
               0x122BB8: [0x000FF000]}
    for count in (1, 2, 3, 0x7F, 0x80):
        add(f'dd40/aligned/{count}', 0x13DD40, [st(1), (B + 0x82, b(count))], aligned, ())
        add(f'e6d0/aligned/{count}', 0x13E6D0, [st(2), (B + 0x82, b(count)), (B + 0x24, w(5)), (B + 0x70, h(1)),
                                                (B + 0x5C, f(30.))], aligned, ())
    for angle in (.3926991, .3926992, 1.):
        add(f'dd40/angle/{angle}', 0x13DD40, [st(1)], {0x1B2BF0: [0], 0x11DF78: [O.fbits(angle)]}, ())
        add(f'e6d0/angle/{angle}', 0x13E6D0, [st(2), (B + 0x24, w(5))], {0x1B2BF0: [0], 0x11DF78: [O.fbits(angle)]}, ())
    for flags in (0, 1, 4, 5, 2, 7):
        for side in (0, 1, 0xFF):
            add(f'1416/flags/{flags}/{side}', 0x1416D0, near, {0x1B2BF0: [flags], 0x1B3670: [side]})
    for count in (0, 1):
        add(f'e6d0/wander/{count}', 0x13E6D0, [st(2), (B + 0x20, w(count))] + near, {0x1B2BF0: [1], 0x122BB8: [-1]})
    pursue = near + [st(1), (B + 0x70, h(1)), (B + 0x5C, f(30.))]
    q = {0x1B2BF0: [1]}
    add('dd40/near', 0x13DD40, near + [st(1), (B + 0x70, h(1)), (B + 0x5C, f(20.))], q)
    add('dd40/recent', 0x13DD40, pursue + [(B + 0x24, w(3))], {0x1B2BF0: [1], 0x1B1560: [0]})
    for ahead, far, wall in ((0, 0, 0), (1, 1, 0), (1, 0, 1), (1, 0, 0)):
        add(f'dd40/charge/{ahead}{far}{wall}', 0x13DD40, pursue,
            {0x1B2BF0: [1], 0x1B1560: [ahead], 0x1B13F0: [far], 0x1B3250: [wall]})
    watch = near + [st(1), (B, O.fw(5., 6., 7., 1.)), (A + 0xB0, O.fw(2., 3., 4., 1.))]
    for seen_, timer, tick in ((1, 0, 0), (1, 0, 1), (1, 3, 0), (0, 0, 0), (0, 0, 1), (0, 3, 0x40)):
        for found in (0, 1):
            add(f'dd40/look/{seen_}/{timer}/{tick}/{found}', 0x13DD40,
                watch + [(B + 0x84, b(seen_)), (B + 0x24, w(timer)), (B + 0x20, w(9)), (FAR[0], w(0x41)),
                         (FAR[1], h(0xFFFF + tick))], {0x1B2BF0: [1], 0x1B13F0: [found]})
    # the turn sub-states: both sides, both probes
    for entry, state in ((0x13DD40, 2), (0x13E6D0, 3)):
        base = [st(state), (B + 0x44, f(1.)), (A + 0xC4, f(.5)), (B + 0x24, w(5)), (B + 0x20, w(9)), (B + 0x70, h(1)),
                (B + 0x5C, f(30.))]
        for alert in (0, 1):
            for side in (0, 1, 2, 0xFF):
                for first, second in ((1, 0), (0, 1), (0, 0)):
                    add(f'{entry:06X}/turn/{alert}/{side}/{first}{second}', entry,
                        base + [(B + 0x70, h(alert)), (B + 0x83, b(side))],
                        {0x1B3250: [first, second], 0x1B12B0: [O.fbits(1.)]})
    # 0013DD40's tail: bark, grunt, the attack table, the fallback
    for bark in (0, 9, 0x109):
        for grunt, timer in ((0, 0), (1, 0), (1, 2)):
            add(f'dd40/tail/{bark}/{grunt}/{timer}', 0x13DD40,
                [st(3), (B + 0x30, w(bark)), (B + 0x72, h(grunt)), (B + 0x28, w(timer)), (B + 0x5C, f(30.)),
                 (B + 0x20, w(4))], {0x122BB8: [-0x7FFFFFFF]})
    for height, roll in ((0., 0), (0., 0x20), (13.5, 0x1E0000), (13.5, 0)):
        for state in (2, 3):
            for blocked in (0, 1):
                for flag in (0, 0x80):
                    add(f'dd40/attack/{height}/{roll}/{state}/{blocked}/{flag}', 0x13DD40,
                        [st(state), (B + 0x83, b(2)), (B + 0x5C, f(20.)), (A + 0xB4, f(height)), (0x810354, f(0.)),
                         (A + 0xD, b(flag))], {0x122BB8: [roll], 0x21BE40: [blocked], 0x11DF78: [O.fbits(height)]}, ())
    for table in range(16):
        add(f'dd40/table/{table}', 0x13DD40, [st(3), (B + 0x5C, f(1.)), (A + 0xB4, f(20.))],
            {0x122BB8: [table << 17]})
    for timer, alert, level, roll in ((1, 0, 0, 0), (0, 1, 1, 0xFF000), (0, 1, 0, 0), (0, 1, 0, 0xE0000), (0, 0, 2, 0x20000)):
        add(f'dd40/idle/{timer}/{alert}/{level}/{roll:x}', 0x13DD40,
            [st(3), (B + 0x5C, f(30.)), (B + 0x20, w(timer)), (B + 0x70, h(alert)), (0x81050C, b(level))],
            {0x122BB8: [roll]})
    # 0013E6D0's tail
    for timer, alert, distance in ((2, 1, 30.), (1, 1, 30.), (0, 1, 30.), (5, 0, 30.), (5, 1, 20.), (5, 1, 20.01)):
        for bark in (0, 9):
            add(f'e6d0/tail/{timer}/{alert}/{distance}/{bark}', 0x13E6D0,
                [st(4), (B + 0x24, w(timer)), (B + 0x70, h(alert)), (B + 0x5C, f(distance)), (B + 0x30, w(bark))],
                {0x122BB8: [-5]})
    # 0013EB90
    for speed in (.6, .61, 0.):
        for flag in (0, 1, 3, 0x81):
            for timer, landed in ((0., 2), (0., 0), (-1., 2), (1., 2)):
                add(f'eb90/{speed}/{flag}/{timer}/{landed}', 0x13EB90,
                    [st(1), (B + 0x4C, f(speed)), (B + 0x86, b(flag)), (B + 0x48, f(timer)), (B + 0x80, b(landed)),
                     (B + 0x60, f(7.))])
    # 0013EDB0
    for value in (.1, .5235988, .5235989, .6, 1.0471976, 1.0471977, 1.2, -1.):
        add(f'edb0/pitch/{value}', 0x13EDB0, [st(1), done], {0x11E620: [O.fbits(value)], 0x1B15D0: [O.fbits(4.)]}, PURE)
    for raw in (TINY, 0x80000001, INF, NINF, NAN):
        add(f'edb0/pitch/raw/{raw:x}', 0x13EDB0, [st(1), done], {0x11E620: [raw]}, PURE)
    for frame in (16., 16.001, 10.):
        for fired in (0, 1):
            for wall in (0, 1):
                for flag in (0, 0x80):
                    for ended in (0, 0x1000):
                        add(f'edb0/spit/{frame}/{fired}/{wall}/{flag}/{ended}', 0x13EDB0,
                            [st(2), (A + 0x3C, f(frame)), (B + 0x20, w(fired)), (A + 0xD, b(flag)),
                             (B + 0x30, w(ended)), (B + 0x54, f(.25)), (A + 0xC4, f(.5))], {0x19A6F0: [wall]})
    # 0013F240
    for flag in (0, 1, 3):
        for hit in (0, 1):
            add(f'f240/swing/{flag}/{hit}', 0x13F240, [st(1), done, (B + 0x86, b(flag)), (B + 0x60, f(7.))], {0x1B3390: [hit]})
    for frame in (15.9, 16., 20.):
        for armed in (0, 1):
            for blocked, seen_ in ((0, 1), (0, 0), (1, 1)):
                for hard in (0, 1):
                    add(f'f240/hit/{frame}/{armed}/{blocked}/{seen_}/{hard}', 0x13F240,
                        [st(2), (A + 0x3C, f(frame)), (B + 0x20, w(armed)), (0x81070A, b(hard)),
                         (0x810360, O.fw(9., 8., 7., 1.)), (A + 0xB0, O.fw(2., 3., 4., 1.))],
                        {0x21BE40: [blocked], 0x1A7B80: [seen_], 0x122BB8: [0x80]})
    for timer in (0., -1., 1.):
        for flag in (0, 1, 3):
            for landed in (0, 2, 0x82):
                add(f'f240/late/{timer}/{flag}/{landed}', 0x13F240,
                    [st(2), (B + 0x20, w(1)), (B + 0x48, f(timer)), (B + 0x86, b(flag)), (B + 0x80, b(landed))])
    add('f240/rest', 0x13F240, [st(3), done, (A + 0xC4, f(.5))])
    # 0013F770: the wall limits and timers around each comparison
    grip = [(A + 0x14C, w(BONES)), (BONES + 0x90, O.fw(*range(1, 17)))]
    for hard in (0, 1):
        for wall in (12., 15., 15.1, 25., 25.1, 30., 30.1):
            for state, roll in ((3, 0), (5, 0)):
                add(f'f770/wall/{hard}/{wall}/{state}', 0x13F770,
                    grip + [st(state), (0x81070A, b(hard)), (0x8104D0, f(wall)), (B + 0x20, w(0xB3))], {0x1A7B80: [1]})
    for count in (0xB2, 0xB3, 0xB4):
        for other in (0x80, 0x81, 0x82):
            add(f'f770/timers/{count:x}/{other:x}', 0x13F770, grip + [st(5), (B + 0x20, w(count)), (B + 0x24, w(other)),
                                                                     (0x8104D0, f(100.))], {0x122BB8: [-1]})
    for state in (6, 8):
        for timer in (0., -1.):
            for flag in (0, 1, 3):
                for landed in (0, 2):
                    add(f'f770/end/{state}/{timer}/{flag}/{landed}', 0x13F770,
                        [st(state), (B + 0x48, f(timer)), (B + 0x86, b(flag)), (B + 0x80, b(landed)), (B + 0x60, f(7.))])
    for speed in (0., -1., 1.):
        for hit in (0, 1):
            add(f'f770/sweep/{speed}/{hit}', 0x13F770, [st(6), (B + 0x48, f(1.)), (B + 0x4C, f(speed))], {0x1B3390: [hit]})
    for state, flag in ((1, 0x1000), (2, 0x1000), (4, 0x1000), (7, 0x1000), (1, 0), (2, 0), (4, 0), (7, 0)):
        add(f'f770/flag/{state}/{flag:x}', 0x13F770, [st(state), (B + 0x30, w(flag)), (B + 0x86, b(1)), (B + 0x60, f(7.))])
    for ready, seen_, timer, landed in ((1, 1, 0., 0), (0, 0, -1., 0), (0, 0, 1., 2), (0, 0, 1., 0), (0, 1, 1., 0)):
        add(f'f770/window/{ready}/{seen_}/{timer}/{landed}', 0x13F770, grip + [
            st(3), (B + 0x48, f(timer)), (B + 0x80, b(landed)), (0x8104D0, f(100.))],
            {0x21BED0: [ready], 0x1A7B80: [seen_]})
    # 00140440
    for flag in (0, 0x1000):
        for scale in (1.1, 1.0999999, 1.2):
            for hard in (0, 1):
                add(f'0440/grown/{flag:x}/{scale}/{hard}', 0x140440,
                    [st(3), (B + 0x30, w(flag)), (A + 0x60, f(scale)), (0x81070A, b(hard)), (A + 0xD, b(1))])
    # 00140760: the clip table's index keeps the sign of the random word
    for roll in (0, 8, 16, 24, -8, -16, -1):
        add(f'0760/clip/{roll}', 0x140760, [], {0x122BB8: [roll]})
    for flag in (0, 0x8000):
        for grab, broke in ((0, 0), (1, 0), (1, 1), (1, 2), (0x80, 1)):
            add(f'0760/grab/{flag:x}/{grab}/{broke}', 0x140760,
                [(A + 0x36, h(flag)), (B + 0x89, b(grab)), (0x8106BD, b(broke))])
            add(f'08d0/grab/{flag:x}/{grab}/{broke}', 0x1408D0, [(B + 0x89, b(grab)), (0x8106BD, b(broke))])
            add(f'0a90/grab/{flag:x}/{grab}/{broke}', 0x140A90,
                [(B + 0x89, b(grab)), (0x8106BD, b(broke)), (B + 0x8C, b(1 if flag else 0))])
    for life in (0, 1):
        add(f'08d0/after/{life}', 0x1408D0, [st(1), done, (A + 0x34, h(life)), (B + 0x80, b(2))])
    for kept in (0, 1, 0x100):
        add(f'0a90/corpse/{kept}', 0x140A90, [st(1), done], {0x1EFE00: [kept]})


def header_checks():
    header = (ROOT / 'src/game/em_level16_creature.h').read_text()
    block = header.split('/* BEGIN GENERATED HOOKS */')[1].split('/* END GENERATED HOOKS */')[0]
    found = re.findall(r'int \(\*(w_[0-9A-F]{8})\)\(void \*ctx((?:, [^,)]+)*)\);', block)
    assert [n for n, _ in found] == [h[0] for h in HOOKS]
    types = {U: 'uint32_t', I: 'int32_t', F: 'float', Q: 'uint64_t'}
    for (name, params), (expected, address, args, result) in zip(found, HOOKS):
        actual = [re.sub(r'\s*\b\w+$', '', p.strip()).replace(' ', '') for p in params.split(',')[1:]]
        assert actual == [types[k] for k, _ in args] + ([types[result] + '*'] if result else [])
    internal = (ROOT / 'src/game/em_level16_creature_internal.h').read_text()
    for name, address, _, _ in HOOKS:
        bodies = re.findall(r'static inline int l16c_c_%s\(.*?\n}\n' % name[2:], internal, re.S)
        assert len(bodies) == 1 and f'o->h->{name}(' in bodies[0] and f'0x{address:08X}u' in bodies[0]


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


def main():
    configure()
    O.NATIVE = O.build(os.environ.get('EM_LEVEL16_CREATURE_SOURCE'))
    header_checks()
    entry_guards()
    selected = cases()
    only = os.environ.get('EM_LEVEL16_CREATURE_ONLY')
    if only:
        selected = [c for c in selected if c[0].startswith(only)]
    O.CASES[:] = selected
    assert len({c[0] for c in selected}) == len(selected)
    O.reference_mode.banner(f'{len(selected)} cases')
    results = O.run_cases(selected)
    failures = [(label, errors) for label, _, errors, _, _, _ in results if errors]
    seen = set().union(*(r[3] for r in results))
    for entry, (_, size, _, _) in FUNCS.items():
        words = O.reachable_words(O.image_for(entry), entry, size)
        missing = words - seen
        print(f'{entry:08X}: {len(words & seen)}/{len(words)} reachable words')
        if missing and not only:
            failures.append(('coverage', [entry, [hex(m) for m in sorted(missing)]]))
    if not only:
        failures += [('callee inventory', p) for p in O.callee_checks()]
        if not failures:
            sites, missing = O.hook_sites(results)
            if missing:
                failures.append(('unreached hooks', missing))
            for problems, runs in O.reference_mode.parallel_map(O.hook_contract_site, sites):
                if problems:
                    failures.append(('fail-stop contract', problems))
    for label, errors in failures[:int(os.environ.get('EM_LEVEL16_CREATURE_SHOW', '8'))]:
        print('FAIL', label, errors)
    assert not failures, f'{len(failures)} failures'
    print(f'PASS {len(selected)} cases: calls, ordered accesses, whole memory, poisoned starts and fail-stop')


if __name__ == '__main__':
    main()
