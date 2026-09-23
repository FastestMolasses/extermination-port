#!/usr/bin/env python3
"""Execute the original player worker routines and compare em_player_misc_workers.c.

docs/PLAYER_MISC_WORKERS.md. The user's pinned ELF supplies every
instruction and table; none are embedded here. Routines executed unmodified:

  001FBD50 sound at an object     001FBF50 positional gain   001B15D0 distance
  00182250 hang aim track         0017E250 ledge ahead       0017E510 ledge above
  0017E7C0 hang side probe        0017DF70 / 0017DFB0 / 0017E0D0 / 0017E150 /
  0017E1D0 side clip requests     0017FF80 clip by +2F1      00182AF0 sound +0x100
  00177B80 ledge depth            0021E650 +7 countdown      0015C1F0 model kind
  001EFE00 effect at the actor    00122BB8 rand (against em_random.c's state)

plus the leaves they call without a worker (00102948, 001031E0, 001281C0,
0011DF78). Every callee that is a worker in the native module is hooked:

- the SDK leaves (VU0 matrix/vector routines, sin, atan2, sqrt, 001B1470,
  001B1380) run their ORIGINAL code through the hook (pass-through), so the
  values the translated routine consumes are the original's;
- the game callees (collision probes, clip requests, sound submit, ...) are
  scripted per case (return value, record bytes, hit record) and recorded.

Each hooked call at depth 0 is logged with every argument (pointers as the
buffer they name, plus the words they point at) together with what it
produced. The native module then runs with workers that check each call
against that log and hand back the same products; the test compares the
worker call sequence, all 0x320 record bytes, every scratchpad word, the
outputs and the return values. It also asserts that the hooked set is
exactly the jal targets of the routines that are not translated here.

Arithmetic: MiscEE (the fall lane's FallEE, imported, not edited) routes
COP1 and VU0 macro ops through tools/ee_float_model.py.

Default run (~10 s): a fixed-seed sample that still exercises both outcomes
of every conditional branch of the translated routines (asserted), the
fault-stop cuts and the missing-worker refusals. EM_TEST_FULL=1: the full
sweep. EM_TEST_WORLD=1: the route beats (docs/PLAYER_MISC_WORKERS.md).
"""
import ctypes as C
import hashlib
import os
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode  # noqa: E402
import test_player_slide_reference as shared  # noqa: E402
from test_player_slide_reference import EE, read_elf, s32, sx32  # noqa: E402
from test_player_fall_reference import FallEE, nested_bits  # noqa: E402

MASK = 0xFFFFFFFF
LANE = os.environ.get('EM_LANE', 'b6-player-misc-workers')
OUT = ROOT / 'build' / LANE


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def B(word):
    return struct.unpack('<f', struct.pack('<I', word & MASK))[0]


# ======================================================================
# The routines, their callees and the world addresses
# ======================================================================

SIZES = {0x1FBD50: 0x60, 0x1FBF50: 0x328, 0x1B15D0: 0x54, 0x182250: 0x190, 0x17E250: 0x2B8,
         0x17E510: 0x1CC, 0x17E7C0: 0x968, 0x17DF70: 0x38, 0x17DFB0: 0x120, 0x17E0D0: 0x78,
         0x17E150: 0x78, 0x17E1D0: 0x78, 0x17FF80: 0x74, 0x182AF0: 0x3C, 0x177B80: 0x168,
         0x21E650: 0x1D4, 0x15C1F0: 0x118, 0x1EFE00: 0xAC, 0x122BB8: 0x30}
# Leaves the translation performs inline or reuses directly (not workers).
INLINE = {0x102948: '00102948 quadword copy', 0x1031E0: '001031E0 three-word copy',
          0x1281C0: '001281C0 float_to_int (em_player_float_to_int)',
          0x11DF78: '0011DF78 fabsf (sign-bit clear)'}

# Worker hooks: address -> (worker name, 'pass' | 'script').
HOOKS = {
    0x1029C0: ('identity', 'pass'), 0x102C58: ('euler', 'pass'), 0x102918: ('translate', 'pass'),
    0x1026A0: ('transform', 'pass'), 0x1028B8: ('vadd', 'pass'), 0x1028D0: ('vsub', 'pass'),
    0x102760: ('normalize', 'pass'), 0x102738: ('dot', 'pass'), 0x11E2A8: ('sine', 'pass'),
    0x11E620: ('atan2', 'pass'), 0x11E748: ('sqrt', 'pass'), 0x1B1470: ('wrap', 'pass'),
    0x1B1380: ('side', 'pass'),
    0x1FB9F0: ('submit', 'script'), 0x179B90: ('sound_base', 'script'),
    0x1749A0: ('request', 'script'), 0x188570: ('clip_2F1_0', 'script'),
    0x188590: ('clip_2F1_1', 'script'), 0x17F1C0: ('ahead', 'script'),
    0x19AD00: ('move', 'script'), 0x19AFE0: ('sweep', 'script'), 0x1760C0: ('column', 'script'),
    0x19AB20: ('ground', 'script'), 0x17E6E0: ('edge', 'script'), 0x1782A0: ('grab', 'script'),
    0x178440: ('grab_33', 'script'), 0x1784E0: ('reach', 'script'),
    0x178910: ('ledge_top', 'script'), 0x17F130: ('blocked', 'script'), 0x1B61C0: ('cue', 'script'),
    0x182870: ('land_sound', 'script'), 0x21D490: ('w0021D490', 'script'),
    0x1CA6E0: ('bind_model', 'script'), 0x1C6150: ('bone_count', 'script'),
    0x200890: ('w00200890', 'script'), 0x1EF9D0: ('spawn', 'script'),
}
NAME_ADDRESS = {name: address for address, (name, _) in HOOKS.items()}

ACTOR = 0x680000
HITNODE = 0x6D0000              # the node record *(0x700031D0) points at
NODE_E = 0x6E0000               # the record the scripted 001EF9D0 hands out
OUTS = 0x6F0000                 # 001FBF50's &a / &b
VEC = 0x7F0E0000                # a stack-region vector (0017E250's arg1 from a stack copy)
TABLE = 0x28A490                # D_0028A490
TABLE_COUNT = 0x48
SCRATCH = (('s3400', 0x70003400, 16), ('s3600', 0x70003600, 4), ('s3610', 0x70003610, 4),
           ('s36A0', 0x700036A0, 16), ('s38A0', 0x700038A0, 4),
           ('s38B0', 0x700038B0, 4), ('s38C0', 0x700038C0, 4), ('s38D0', 0x700038D0, 4),
           ('s38E0', 0x700038E0, 4), ('s38F0', 0x700038F0, 4), ('s3900', 0x70003900, 4),
           ('s3910', 0x70003910, 4), ('s3A20', 0x70003A20, 1), ('s3A24', 0x70003A24, 1))


def in_translated(pc):
    return any(start <= pc < start + size for start, size in SIZES.items())


def check_callee_set(elf):
    """The jal targets of the translated routines are exactly the hooked
    workers, the inline leaves and the routines translated here."""
    ee = EE(elf)
    targets = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            if word >> 26 == 3:
                targets.add((word & 0x3FFFFFF) << 2)
    missing = sorted(t for t in targets if t not in HOOKS and t not in INLINE and t not in SIZES)
    assert not missing, ('callees neither hooked, inline nor translated', [hex(t) for t in missing])
    unused = sorted(t for t in HOOKS if t not in targets)
    assert not unused, ('hooked addresses no routine calls', [hex(t) for t in unused])
    return len(targets)


def branch_sites(elf):
    ee = EE(elf)
    sites = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if op in (4, 20) and rs == 0 and rt == 0:
                continue
            if ee.branch(word, pc) is not None:
                sites.add(pc)
    return sites


class MiscEE(FallEE):
    """FallEE (COP1/VU0 through ee_float_model) recording the branch outcomes
    inside the translated routines."""

    def __init__(self, elf):
        super().__init__(elf)
        self.outcomes = set()

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and in_translated(pc):
            self.outcomes.add((pc, b[0]))
        return b


# ======================================================================
# Argument tags: the buffer a pointer names, identical on both sides
# ======================================================================

def ee_tag(address):
    address &= MASK
    if ACTOR <= address < ACTOR + 0x320:
        return ('A', address - ACTOR)
    if 0x70000000 <= address < 0x70004000:
        return ('S', address)
    if 0x810360 <= address < 0x810370 or 0x8105D0 <= address < 0x8105E0:
        return ('D', address)
    if 0x7F000000 <= address < 0x7F100000:
        return ('stack',)
    return ('?', address)


# (worker, argument layout). Layout letters: A = the record (asserted), P4 /
# P16 = a pointer and its 4 / 16 words, I = s32 int, U = u32, F = float bits.
LAYOUT = {
    'identity': ('Pn',), 'euler': ('Pn', 'P16', 'P4'), 'translate': ('Pn', 'P16', 'P4'),
    'transform': ('Pn', 'P16', 'P4'), 'vadd': ('Pn', 'P4', 'P4'), 'vsub': ('Pn', 'P4', 'P4'),
    'normalize': ('Pn', 'P4'), 'dot': ('P4', 'P4'), 'sine': ('F',), 'atan2': ('F', 'F'),
    'sqrt': ('F',), 'wrap': ('F',), 'side': ('P4', 'P4', 'F'),
    'submit': ('I', 'I', 'I', 'I'), 'sound_base': ('A',), 'request': ('A', 'I', 'I', 'F'),
    'clip_2F1_0': ('A',), 'clip_2F1_1': ('A',), 'ahead': ('A',), 'move': ('A', 'P4', 'U'),
    'sweep': ('A', 'P4', 'P4', 'U'), 'column': ('A', 'P4', 'I', 'F'),
    'ground': ('A', 'P4', 'X280', 'U'), 'edge': ('A', 'I', 'F', 'F'), 'grab': ('A',),
    'grab_33': ('A',), 'reach': ('A',), 'ledge_top': ('A', 'I'), 'blocked': ('A', 'I'),
    'cue': ('I', 'I', 'I', 'I'), 'land_sound': ('A', 'I'), 'w0021D490': ('A',),
    'bind_model': ('A', 'U'), 'bone_count': ('U',), 'w00200890': (), 'spawn': ('U', 'P4', 'F'),
}
# What a pass-through leaf produces: ('out', words) at its a0, or a register.
PRODUCT = {'identity': ('out', 16), 'euler': ('out', 16), 'translate': ('out', 16),
           'transform': ('out', 4), 'vadd': ('out', 4), 'vsub': ('out', 4),
           'normalize': ('out', 4), 'dot': ('f0',), 'sine': ('f0',), 'atan2': ('f0',),
           'sqrt': ('f0',), 'wrap': ('f0',), 'side': ('v0',)}


def ee_entry(ee, name):
    ints, floats = 0, 0
    entry = [name]
    for kind in LAYOUT[name]:
        if kind in ('F',):
            entry.append(ee.f[12 + floats] & MASK); floats += 1
            continue
        value = ee.arg(ints); ints += 1
        if kind == 'A':
            assert value == ACTOR, (name, 'record argument', hex(value))
        elif kind == 'X280':
            assert value == ACTOR + 0x280, (name, '+280 argument', hex(value))
        elif kind == 'I':
            entry.append(s32(value))
        elif kind == 'U':
            entry.append(value)
        elif kind == 'Pn':
            entry.append(ee_tag(value))
        else:
            n = int(kind[1:])
            entry.append(ee_tag(value))
            entry.append(tuple(ee.load(value + 4 * i) for i in range(n)))
    return tuple(entry)


# ======================================================================
# Scripted callee effects
# ======================================================================

def effect_for(rng, name, case):
    """What a scripted callee does: v0, record bytes (offset, size, value),
    hit record changes and, for 001EF9D0, the record it hands out."""
    e = {'ret': 0, 'writes': [], 'node': [], 'point': [], 'spawn': None}
    chance = rng.random
    if name == 'move':
        e['ret'] = rng.choice((0, 1, 1, 2, 4))
        if chance() < 0.5: e['point'] = [(i, F(rng.uniform(-400, 400))) for i in range(3)]
        if chance() < 0.2: e['writes'].append((0xB4, 4, F(rng.uniform(-50, 50))))
    elif name == 'sweep':
        e['ret'] = rng.choice((0, 0, 1, 2, 4, 6, 7))
        if chance() < 0.8:
            e['node'].append((0x1A, 1, rng.choice((0x32, 0x3B, 0x33, 0x3D, 0x3D, 0x31, 0x00))))
        if chance() < 0.3: e['point'] = [(1, F(rng.uniform(-100, 100)))]
    elif name in ('column', 'ground'):
        e['ret'] = rng.choice((0, 0, 1, 2))
        if name == 'ground' and chance() < 0.5:
            e['writes'].append((0x280 + 4 * rng.randrange(3), 4, F(rng.uniform(-9, 9))))
    elif name == 'edge':
        e['ret'] = 0 if chance() < case['edge_zero'] else rng.choice((1, 2))
    elif name in ('grab', 'grab_33', 'reach', 'ledge_top', 'blocked', 'ahead'):
        e['ret'] = rng.choice((0, 1)) if chance() < 0.9 else rng.choice((-1, 7))
        if name in ('grab', 'reach') and e['ret'] and chance() < 0.3:
            e['writes'].append((0x2E0, 4, F(rng.uniform(-300, 300))))
    elif name in ('clip_2F1_0', 'clip_2F1_1'):
        e['ret'] = rng.choice((0x70, 0x71, 0x1C3, -1, -0x8000, 0x7FFF))
    elif name == 'sound_base':
        e['ret'] = rng.choice((0, 1, 2, 3, 4, -0x101))
    elif name == 'submit':
        e['ret'] = rng.choice((0, 5, 17, -1, 0x12345))
    elif name in ('request', 'land_sound', 'w0021D490', 'cue', 'w00200890'):
        if name != 'cue' and name != 'w00200890' and chance() < 0.3:
            e['writes'].append((rng.choice((0x200, 0x20C, 0x3C)), 4, rng.getrandbits(32)))
    elif name == 'bind_model':
        e['writes'].append((0x44, 4, rng.choice((0x01234560, 0x00ABCDE0, 0))))
    elif name == 'bone_count':
        e['ret'] = rng.choice((0, 1, 23, 56, 0xFF, 0x1FF))
    elif name == 'spawn':
        e['spawn'] = rng.choice((0, NODE_E, NODE_E))
        e['ret'] = e['spawn']
    return e


class Script:
    """Per-case callee effects; case['force'][name] pins the return value (and,
    for a sweep, the surface byte) of that callee's first calls, so the deep
    paths of 0017E7C0 are reached in a short run."""

    def __init__(self, seed, case):
        self.seed, self.case, self.count, self.per_name = seed, case, 0, {}

    def next(self, name):
        rng = random.Random('%d:%d:%s' % (self.seed, self.count, name))
        self.count += 1
        e = effect_for(rng, name, self.case)
        k = self.per_name.get(name, 0)
        self.per_name[name] = k + 1
        forced = self.case.get('force', {}).get(name, ())
        if k < len(forced):
            ret, surface = forced[k]
            e['ret'] = ret
            if surface is not None:
                e['node'] = [(0x1A, 1, surface)]
        return e


# ======================================================================
# Cases
# ======================================================================

ENTRIES = ('gain', 'sound', 'sound300', 'aim', 'ahead', 'ahead_stack', 'above', 'side', 'DF70', 'DFB0',
           'E0D0', 'E150', 'E1D0', 'FF80', 'AF0', 'depth', 'E650', 'C1F0', 'EFE00')
ENTRY_ADDRESS = {'gain': 0x1FBF50, 'sound': 0x1FBD50, 'sound300': 0x1FBD50, 'aim': 0x182250, 'ahead': 0x17E250,
                 'ahead_stack': 0x17E250, 'above': 0x17E510, 'side': 0x17E7C0,
                 'DF70': 0x17DF70, 'DFB0': 0x17DFB0, 'E0D0': 0x17E0D0, 'E150': 0x17E150,
                 'E1D0': 0x17E1D0, 'FF80': 0x17FF80, 'AF0': 0x182AF0, 'depth': 0x177B80,
                 'E650': 0x21E650, 'C1F0': 0x15C1F0, 'EFE00': 0x1EFE00}
WEIGHTS = {'gain': 8, 'sound': 5, 'sound300': 3, 'aim': 5, 'ahead': 3, 'ahead_stack': 2, 'above': 3, 'side': 16,
           'DF70': 1, 'DFB0': 3, 'E0D0': 1, 'E150': 1, 'E1D0': 1, 'FF80': 2, 'AF0': 2,
           'depth': 3, 'E650': 5, 'C1F0': 3, 'EFE00': 2}


def put(buf, offset, size, value):
    buf[offset:offset + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')


def side_force(rng):
    """0017E7C0 deep paths: the climbing box branch (+D 1, area 8 room 3, the
    first sweep clear, the second hitting a surface that is not 0x32/0x3B)
    and the ledge-end sweeps after the full edge chain."""
    surface = rng.choice((0x31, 0x3D, 0x33, 0x00))
    if rng.random() < 0.5:
        return {'edge': [(0, None)] * 3, 'sweep': [(0, None), (rng.choice((2, 4, 6)), surface)]}
    stop = rng.randrange(9)          # the edge probe that reports first (8: none)
    return {'edge': [(0, None)] * stop + [(1, None)], 'blocked': [(0, None)], 'reach': [(0, None)],
            'sweep': [(rng.choice((0, 1, 2, 6)), surface), (rng.choice((0, 1, 0, 6)), None),
                      (rng.choice((0, 3)), None)]}


def make_case(seed):
    import math
    rng = random.Random(seed)
    entry = rng.choices(ENTRIES, weights=[WEIGHTS[e] for e in ENTRIES])[0]
    actor = bytearray(rng.getrandbits(8) for _ in range(0x320))
    f = lambda v: F(v)
    # positions and angles as the player keeps them
    listener = [rng.uniform(-400, 400), rng.uniform(-50, 300), rng.uniform(-400, 400)]
    offset = rng.choice((0.0, 5.0, 17.9, 18.0, 25.0, 150.0, 299.0, 301.0, 600.0))
    direction = rng.uniform(-math.pi, math.pi)
    lift = rng.uniform(-30, 30)
    obj = [listener[0] + offset * math.cos(direction), listener[1] + lift,
           listener[2] + offset * math.sin(direction)]
    if rng.random() < 0.15:          # a distance of exactly 18.0 (the 001FC134 boundary)
        listener = [float(rng.randrange(-300, 300)) for _ in range(3)]
        obj = list(listener)
        obj[rng.choice((0, 2))] += rng.choice((18.0, -18.0))
    for i in range(3):
        put(actor, 0xB0 + 4 * i, 4, f(obj[i]))
        put(actor, 0xC0 + 4 * i, 4, f(rng.uniform(-3.2, 3.2)))
    put(actor, 0xBC, 4, f(rng.choice((1.0, 0.0))))
    for i in range(16):
        put(actor, 0xD0 + 4 * i, 4, f(rng.uniform(-2, 2) if i < 12 else rng.uniform(-300, 300)))
    put(actor, 0x10C, 4, f(1.0))
    put(actor, 0xD, 1, rng.choice((0, 1, 1, 2)))
    put(actor, 0x315, 1, rng.choice((0, 0, 1, 5)))
    put(actor, 0x2F1, 1, rng.choice((0, 1, 2)))
    put(actor, 0x234, 1, rng.choice((0, 1, 2, 7)))
    put(actor, 7, 1, rng.choice((0, 1, 2, 3, 4, 5, 9)))
    put(actor, 0x3C, 4, f(rng.choice((173.0, 173.00002, 172.99998, 105.0, 105.00001, 85.0, 85.00001,
                                      60.0, 60.000004, 45.0, 45.000004, 0.0, 200.0, -1.0))))
    box = rng.random() < 0.5
    if box:
        put(actor, 0xB0, 4, f(rng.choice((119.99999, 120.0, 120.00001, 125.0, 129.99998, 130.0, 131.0))))
        put(actor, 0xB8, 4, f(rng.choice((159.99998, 160.0, 160.00002, 165.0, 169.99998, 170.0, 171.0))))
    hit_nx, hit_nz = rng.uniform(-1, 1), rng.uniform(-1, 1)
    yaw = 4.712389 + math.atan2(-hit_nz, hit_nx)
    while yaw > math.pi: yaw -= 2 * math.pi
    put(actor, 0xC4, 4, f(yaw + rng.choice((0.0, 0.1, -0.2, 0.43, -0.44, 0.5, 2.0))))
    node = bytearray(rng.getrandbits(8) for _ in range(0x40))
    put(node, 0x24, 4, f(hit_nx)); put(node, 0x2C, 4, f(hit_nz))
    put(node, 0x1A, 1, rng.choice((0x32, 0x3B, 0x33, 0x3D)))
    scratch = {name: [rng.getrandbits(32) for _ in range(n)] for name, _, n in SCRATCH}
    eye = [listener[0] + rng.uniform(-80, 80), listener[1] + rng.uniform(0, 60),
           listener[2] + rng.uniform(-80, 80)]
    ledge = [rng.uniform(-300, 300) for _ in range(3)] + [rng.uniform(-1, 1) for _ in range(3)]
    ledge_matrix = [F(rng.uniform(-1, 1)) if i < 12 else F(rng.uniform(-300, 300)) for i in range(16)]
    force = side_force(rng) if entry == 'side' and rng.random() < 0.4 else {}
    area, room = rng.choice((8, 8, 0x11, 4, 0)), rng.choice((3, 3, 2))
    if force and 'blocked' not in force:
        actor[0xD] = 1
        if rng.random() < 0.7: area, room = 8, 3
    elif force:
        actor[0xD] = rng.choice((0, 2))
    return {
        'seed': seed, 'entry': entry, 'actor': bytes(actor), 'node': bytes(node),
        'point': [F(rng.uniform(-400, 400)) for _ in range(4)],
        'scratch': scratch,
        'area': area, 'room': room,
        'sel': rng.choice((0, 1, 2, 3)), 'mono': rng.choice((0, 0, 0, 1, 2)),
        'listener': [F(v) for v in listener] + [F(1.0)], 'yaw': F(rng.uniform(-3.1, 3.1)),
        'eye': [F(v) for v in eye] + [F(1.0)],
        'table': [rng.getrandbits(32) for _ in range(TABLE_COUNT)],
        'ledge': [F(v) for v in ledge], 'ledge_matrix': ledge_matrix,
        'vec': [F(rng.uniform(-300, 300)) for _ in range(3)] + [F(1.0)],
        'side': rng.choice((0, 0, 1, 1, 2, 0x100, -1)),
        'blend': F(rng.choice((1.0, 4.0, 8.0, 16.0, 0.5))),
        'flat': rng.choice((0, 0, 1, 0x100, 0xFF)),
        'radius': F(rng.choice((300.0, 300.0, 450.0, 18.0, 150.0))),
        'scale': F(rng.choice((4096.0, 1.0, 100.0))),
        'id': rng.choice((0x105, 0x14E, 0x80000027, 0x80000040, 0x80000051)),
        'y': F(rng.uniform(-40, 40)),
        'edge_zero': rng.choice((0.6, 0.93, 1.0)),
        'force': force,
    }


# ======================================================================
# The original side
# ======================================================================

class Oracle:
    def __init__(self, elf):
        self.ee = MiscEE(elf)
        for address, (name, mode) in HOOKS.items():
            self.ee.hooks[address] = self.hook(address, name, mode)
        self.depth = 0

    def hook(self, address, name, mode):
        def run(ee):
            if mode == 'pass':
                entry = ee_entry(ee, name) if self.depth == 0 else None
                saved = ee.hooks.pop(address)
                self.depth += 1
                try:
                    v0, f0 = nested_bits(ee, address, tuple(ee.arg(i) for i in range(4)),
                                         tuple(ee.f[12 + i] for i in range(3)))
                finally:
                    self.depth -= 1
                    ee.hooks[address] = saved
                ee.r[2], ee.f[0] = v0, f0
                if entry is None:
                    return
                kind = PRODUCT[name]
                if kind[0] == 'out':
                    product = ('out', tuple(ee.load(ee.arg(0) + 4 * i) for i in range(kind[1])))
                elif kind[0] == 'f0':
                    product = ('f0', f0 & MASK)
                else:
                    product = ('v0', s32(v0))
                self.log.append((entry, product))
                return
            assert self.depth == 0, (name, 'scripted callee inside a pass-through leaf')
            entry = ee_entry(ee, name)
            e = self.script.next(name)
            for offset, size, value in e['writes']:
                ee.save(ACTOR + offset, value, size)
            for offset, size, value in e['node']:
                ee.save(HITNODE + offset, value, size)
            for index, value in e['point']:
                ee.save(0x700031B0 + 4 * index, value)
            ee.ret_int(e['ret'])
            self.log.append((entry, ('script', e)))
        return run

    def setup(self, case):
        ee = self.ee
        ee.r, ee.rh = [0] * 32, [0] * 32
        ee.f, ee.acc, ee.cond = [0] * 32, 0, False
        ee.vacc, ee.q = [0, 0, 0, 0], 0
        ee.r[28], ee.r[29] = 0x27D370, shared.STACK_TOP
        ee.write(ACTOR, case['actor'])
        ee.write(HITNODE, case['node'])
        ee.save(0x700031D0, HITNODE)
        for i in range(4): ee.save(0x700031B0 + 4 * i, case['point'][i])
        for name, address, n in SCRATCH:
            for i in range(n): ee.save(address + 4 * i, case['scratch'][name][i])
        ee.save(0x810700, case['area'], 1)
        ee.save(0x810701, case['room'], 1)
        ee.save(0x810C60, case['sel'], 1)
        ee.save(0x28215B, case['mono'], 1)
        for i in range(4):
            ee.save(0x810360 + 4 * i, case['listener'][i])
            ee.save(0x8105D0 + 4 * i, case['eye'][i])
        ee.save(0x81027C, case['yaw'])
        for i, value in enumerate(case['table']): ee.save(TABLE + 4 * i, value)
        for i in range(3):
            ee.save(0x70003050 + 4 * i, case['ledge'][i])
            ee.save(0x70003060 + 4 * i, case['ledge'][3 + i])
        for i in range(16): ee.save(0x70003070 + 4 * i, case['ledge_matrix'][i])
        for i in range(4): ee.save(VEC + 4 * i, case['vec'][i])
        ee.write(NODE_E, bytes(0x110))
        ee.write(OUTS, bytes(8))

    def run(self, case):
        ee = self.ee
        self.script, self.log = Script(case['seed'], case), []
        self.setup(case)
        entry, a = case['entry'], ENTRY_ADDRESS[case['entry']]
        ints, floats = (ACTOR,), ()
        if entry == 'gain':
            ints, floats = (ACTOR, OUTS, OUTS + 4, case['flat']), (case['radius'], case['scale'])
        elif entry == 'sound':
            ints, floats = (ACTOR, case['id'], case['flat']), (case['radius'],)
        elif entry == 'sound300':
            ints, floats = (ACTOR, case['id'], 0), (F(300.0),)
        elif entry == 'ahead':
            ints = (ACTOR, ACTOR + 0xB0)
        elif entry == 'ahead_stack':
            ints = (ACTOR, VEC)
        elif entry in ('side',):
            ints = (ACTOR, case['side'])
        elif entry in ('DF70', 'DFB0', 'E0D0', 'E150', 'E1D0'):
            ints, floats = (ACTOR, case['side']), (case['blend'],)
        elif entry == 'FF80':
            floats = (case['blend'],)
        elif entry == 'depth':
            floats = (case['y'],)
        elif entry == 'EFE00':
            ints = (case['id'], ACTOR)
        for i, value in enumerate(ints): ee.r[4 + i] = sx32(value)
        for i, value in enumerate(floats): ee.f[12 + i] = value & MASK
        ee.r[31] = shared.RETURN
        ee.run(a)
        return {
            'log': self.log, 'v0': s32(ee.r[2]),
            'actor': ee.read(ACTOR, 0x320),
            'scratch': {name: [ee.load(address + 4 * i) for i in range(n)] for name, address, n in SCRATCH},
            'outs': (s32(ee.load(OUTS)), s32(ee.load(OUTS + 4))),
            'node': ee.read(NODE_E, 0x110),
        }


# ======================================================================
# The native side
# ======================================================================

P, VP, I32, U32, FLT = C.POINTER, C.c_void_p, C.c_int32, C.c_uint32, C.c_float


class LiveActor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', VP), ('link_prev', VP),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


class Scratch(C.Structure):
    _fields_ = [(name, U32 * n) if n > 1 else (name, U32) for name, _, n in SCRATCH]


class Scene(C.Structure):
    _fields_ = [('d810700', C.c_uint8), ('d810701', C.c_uint8), ('d810C60', C.c_uint8),
                ('d28215B', C.c_uint8), ('d810360', U32 * 4), ('d81027C', U32),
                ('d8105D0', U32 * 4), ('d28A490', P(U32)), ('d28A490_count', U32)]


class Ledge(C.Structure):
    _fields_ = [('point', U32 * 3), ('normal', U32 * 3), ('heading', U32), ('matrix', U32 * 16)]


class View(C.Structure):
    _fields_ = [('w24', P(U32)), ('pos', VP), ('rot', VP)]


FN = {
    'identity': C.CFUNCTYPE(I32, VP, VP), 'euler': C.CFUNCTYPE(I32, VP, VP, VP, VP),
    'translate': C.CFUNCTYPE(I32, VP, VP, VP, VP), 'transform': C.CFUNCTYPE(I32, VP, VP, VP, VP),
    'vadd': C.CFUNCTYPE(I32, VP, VP, VP, VP), 'vsub': C.CFUNCTYPE(I32, VP, VP, VP, VP),
    'normalize': C.CFUNCTYPE(I32, VP, VP, VP), 'dot': C.CFUNCTYPE(I32, VP, VP, VP, VP),
    'sine': C.CFUNCTYPE(I32, VP, FLT, VP), 'atan2': C.CFUNCTYPE(I32, VP, FLT, FLT, VP),
    'sqrt': C.CFUNCTYPE(I32, VP, FLT, VP), 'wrap': C.CFUNCTYPE(I32, VP, FLT, VP),
    'side': C.CFUNCTYPE(I32, VP, VP, VP, FLT, VP),
    'submit': C.CFUNCTYPE(I32, VP, I32, I32, I32, I32, VP),
    'sound_base': C.CFUNCTYPE(I32, VP, VP, VP),
    'request': C.CFUNCTYPE(I32, VP, VP, I32, I32, FLT),
    'clip_2F1_0': C.CFUNCTYPE(I32, VP, VP, VP), 'clip_2F1_1': C.CFUNCTYPE(I32, VP, VP, VP),
    'ahead': C.CFUNCTYPE(I32, VP, VP, VP),
    'move': C.CFUNCTYPE(I32, VP, VP, VP, U32, VP),
    'sweep': C.CFUNCTYPE(I32, VP, VP, VP, VP, U32, VP),
    'column': C.CFUNCTYPE(I32, VP, VP, VP, I32, FLT, VP),
    'ground': C.CFUNCTYPE(I32, VP, VP, VP, U32, VP),
    'hit_node_word': C.CFUNCTYPE(I32, VP, U32, VP), 'hit_node_byte': C.CFUNCTYPE(I32, VP, U32, VP),
    'hit_point_word': C.CFUNCTYPE(I32, VP, U32, VP),
    'edge': C.CFUNCTYPE(I32, VP, VP, I32, FLT, FLT, VP),
    'grab': C.CFUNCTYPE(I32, VP, VP, VP), 'grab_33': C.CFUNCTYPE(I32, VP, VP, VP),
    'reach': C.CFUNCTYPE(I32, VP, VP, VP),
    'ledge_top': C.CFUNCTYPE(I32, VP, VP, I32, VP), 'blocked': C.CFUNCTYPE(I32, VP, VP, I32, VP),
    'cue': C.CFUNCTYPE(I32, VP, I32, I32, I32, I32), 'land_sound': C.CFUNCTYPE(I32, VP, VP, I32),
    'w0021D490': C.CFUNCTYPE(I32, VP, VP), 'bind_model': C.CFUNCTYPE(I32, VP, VP, U32),
    'bone_count': C.CFUNCTYPE(I32, VP, U32, VP), 'w00200890': C.CFUNCTYPE(I32, VP),
    'spawn': C.CFUNCTYPE(I32, VP, U32, VP, FLT, VP, VP),
}
WORKER_FIELDS = ('identity', 'euler', 'translate', 'transform', 'vadd', 'vsub', 'normalize', 'dot',
                 'sine', 'atan2', 'sqrt', 'wrap', 'side', 'submit', 'sound_base', 'request',
                 'clip_2F1_0', 'clip_2F1_1', 'ahead', 'move', 'sweep', 'column', 'ground',
                 'hit_node_word', 'hit_node_byte', 'hit_point_word', 'edge', 'grab', 'grab_33',
                 'reach', 'ledge_top', 'blocked', 'cue', 'land_sound', 'w0021D490', 'bind_model',
                 'bone_count', 'w00200890', 'spawn')
HIT_READERS = ('hit_node_word', 'hit_node_byte', 'hit_point_word')


class Workers(C.Structure):
    _fields_ = [('context', VP)] + [(name, FN[name]) for name in WORKER_FIELDS]


class Host(C.Structure):
    _fields_ = [('workers', P(Workers)), ('scene', P(Scene)), ('scratch', P(Scratch))]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('misc.dylib' if sys.platform == 'darwin' else 'misc.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'src/game/em_player_misc_workers.c', 'src/game/em_player_stage_workers.c',
                    'src/game/em_random.c', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    H, A = P(Host), P(LiveActor)
    sig = {
        'em_player_misc_001FBF50': [H, VP, P(I32), P(I32), I32, FLT, FLT, P(I32)],
        'em_player_misc_001FBD50': [H, VP, I32, I32, FLT, P(I32)],
        'em_player_misc_00182250': [H, A],
        'em_player_misc_0017E250': [H, A, VP, P(I32)],
        'em_player_misc_0017E510': [H, A, P(I32)],
        'em_player_misc_0017E7C0': [H, A, I32, P(I32)],
        'em_player_misc_0017DF70': [H, A, I32, FLT], 'em_player_misc_0017DFB0': [H, A, I32, FLT],
        'em_player_misc_0017E0D0': [H, A, I32, FLT], 'em_player_misc_0017E150': [H, A, I32, FLT],
        'em_player_misc_0017E1D0': [H, A, I32, FLT], 'em_player_misc_0017FF80': [H, A, FLT],
        'em_player_misc_00182AF0': [H, A],
        'em_player_misc_00177B80': [H, A, P(Ledge), FLT, P(I32)],
        'em_player_misc_0021E650': [H, A], 'em_player_misc_0015C1F0': [H, A],
        'em_player_misc_001EFE00': [H, U32, A, P(U32)],
        'em_player_misc_random': [VP, P(U32)], 'em_player_misc_random_i32': [VP, P(I32)],
        # the adapters in the consumers' worker shapes (context = the host)
        'em_player_misc_w_sound': [VP, A, C.c_int, C.c_int, FLT],
        'em_player_misc_w_sound_300': [VP, A, C.c_uint],
        'em_player_misc_w_sound_300_i': [VP, A, C.c_int],
        'em_player_misc_w_sound_300_handle': [VP, A, C.c_int, P(C.c_int)],
        'em_player_misc_w_001FBF50': [VP, VP, FLT, FLT, P(I32), P(I32), P(I32)],
        'em_player_misc_w_aim_track': [VP, A],
        'em_player_misc_w_ledge_ahead_self': [VP, A, P(C.c_int)],
        'em_player_misc_w_ledge_ahead': [VP, A, VP, P(C.c_int)],
        'em_player_misc_w_ledge_above': [VP, A, P(C.c_int)],
        'em_player_misc_w_ledge_side': [VP, A, C.c_int, P(C.c_int)],
        'em_player_misc_w_clip_DF70': [VP, A, C.c_int, FLT], 'em_player_misc_w_clip_DFB0': [VP, A, C.c_int, FLT],
        'em_player_misc_w_clip_E0D0': [VP, A, C.c_int, FLT], 'em_player_misc_w_clip_E150': [VP, A, C.c_int, FLT],
        'em_player_misc_w_clip_E1D0': [VP, A, C.c_int, FLT], 'em_player_misc_w_clip_FF80': [VP, A, FLT],
        'em_player_misc_w_sound_100': [VP, A],
        'em_player_misc_w_depth': [VP, A, P(Ledge), FLT, P(C.c_int)],
        'em_player_misc_w_0021E650': [VP, A], 'em_player_misc_w_0015C1F0': [VP, A],
        'em_player_misc_w_001EFE00': [VP, U32, A],
        'em_player_misc_w_attach': [VP, A, U32, P(U32)],
    }
    for name, args in sig.items():
        getattr(native, name).argtypes = args
        getattr(native, name).restype = C.c_int
    return native


class Native:
    """em_player_misc_workers.c with workers that replay the original's log."""

    def __init__(self, native, case, want, fail_at=None, missing=None):
        self.native, self.case, self.want = native, case, want
        self.fail_at, self.calls, self.error = fail_at, 0, None
        self.hit_used = set()
        self.live = LiveActor()
        C.memmove(self.live.bytes, case['actor'], 0x320)
        self.scratch = Scratch()
        for name, _, n in SCRATCH:
            if n > 1:
                for i in range(n): getattr(self.scratch, name)[i] = case['scratch'][name][i]
            else:
                setattr(self.scratch, name, case['scratch'][name][0])
        self.table = (U32 * TABLE_COUNT)(*case['table'])
        self.scene = Scene(case['area'], case['room'], case['sel'], case['mono'],
                           (U32 * 4)(*case['listener']), case['yaw'], (U32 * 4)(*case['eye']),
                           C.cast(self.table, P(U32)), TABLE_COUNT)
        self.ledge = Ledge((U32 * 3)(*case['ledge'][:3]), (U32 * 3)(*case['ledge'][3:]), 0,
                           (U32 * 16)(*case['ledge_matrix']))
        self.vec = (U32 * 4)(*case['vec'])
        self.node_bytes = bytearray(case['node'])
        self.point = list(case['point'])
        self.node_e = (C.c_uint8 * 0x110)()
        self.keep, fields = [], {}
        for name in WORKER_FIELDS:
            fields[name] = FN[name]() if name == missing else FN[name](self.guard(name))
            self.keep.append(fields[name])
        self.workers = Workers(None, **fields)
        self.host = Host(C.pointer(self.workers), C.pointer(self.scene), C.pointer(self.scratch))
        if missing == 'scene': self.host.scene = P(Scene)()
        if missing == 'scratch': self.host.scratch = P(Scratch)()
        self.log = []
        self.regions = self.region_table()

    # ---- pointers -> the buffers they name ----
    def region_table(self):
        rows = [(C.addressof(self.live.bytes), 0x320, lambda off: ('A', off))]
        for name, address, n in SCRATCH:
            rows.append((C.addressof(self.scratch) + getattr(Scratch, name).offset, 4 * n,
                         lambda off, a=address: ('S', a + off)))
        rows.append((C.addressof(self.scene) + Scene.d810360.offset, 16, lambda off: ('D', 0x810360 + off)))
        rows.append((C.addressof(self.scene) + Scene.d8105D0.offset, 16, lambda off: ('D', 0x8105D0 + off)))
        rows.append((C.addressof(self.ledge) + Ledge.matrix.offset, 64, lambda off: ('S', 0x70003070 + off)))
        return rows

    def tag(self, pointer):
        for start, size, fn in self.regions:
            if start <= pointer < start + size:
                return fn(pointer - start)
        return ('stack',)

    @staticmethod
    def words(pointer, n):
        return tuple(C.cast(pointer, P(U32))[i] for i in range(n))

    def entry(self, name, args):
        entry, ai = [name], 0
        for kind in LAYOUT[name]:
            if kind in ('A', 'X280'):
                if kind == 'A':
                    assert args[ai] == C.addressof(self.live), (name, 'record pointer')
                    ai += 1
                continue
            value = args[ai]; ai += 1
            if kind == 'F':
                entry.append(F(value) if value == value else struct.unpack('<I', struct.pack('<f', value))[0])
            elif kind == 'I':
                entry.append(s32(value))
            elif kind == 'U':
                entry.append(value & MASK)
            elif kind == 'Pn':
                entry.append(self.tag(value))
            else:
                n = int(kind[1:])
                entry.append(self.tag(value)); entry.append(self.words(value, n))
        return tuple(entry)

    # ---- the workers ----
    def guard(self, name):
        def run(_context, *args):
            if self.error is not None:
                return -1
            try:
                return self.work(name, args)
            except BaseException as error:        # surfaced after the native call returns
                self.error = error
                return -1
        return run

    def work(self, name, args):
        if name in HIT_READERS:
            self.hit_used.add(name)
            offset, out = args
            if name == 'hit_node_word':
                C.cast(out, P(U32))[0] = int.from_bytes(self.node_bytes[offset:offset + 4], 'little')
            elif name == 'hit_node_byte':
                C.cast(out, P(C.c_uint8))[0] = self.node_bytes[offset]
            else:
                C.cast(out, P(U32))[0] = self.point[offset // 4]
            return 0
        index = self.calls
        self.calls += 1
        # arguments: everything before the output pointer(s)
        n_in = {'dot': 2, 'sine': 1, 'atan2': 2, 'sqrt': 1, 'wrap': 1, 'side': 3, 'submit': 4,
                'sound_base': 1, 'clip_2F1_0': 1, 'clip_2F1_1': 1, 'ahead': 1, 'move': 3,
                'sweep': 4, 'column': 4, 'ground': 3, 'edge': 4, 'grab': 1, 'grab_33': 1,
                'reach': 1, 'ledge_top': 2, 'blocked': 2, 'bone_count': 1, 'spawn': 3}.get(name)
        inputs = args if n_in is None else args[:n_in]
        entry = self.entry(name, inputs)
        self.log.append(entry)
        if self.fail_at == index:
            return -1
        assert index < len(self.want['log']), (name, 'call beyond the original sequence', entry)
        want_entry, product = self.want['log'][index]
        assert entry == want_entry, (index, 'worker call differs', entry, want_entry)
        if product[0] == 'out':
            for i, value in enumerate(product[1]): C.cast(args[0], P(U32))[i] = value
            return 0
        if product[0] == 'f0':
            C.cast(args[-1], P(U32))[0] = product[1]
            return 0
        if product[0] == 'v0':
            C.cast(args[-1], P(I32))[0] = product[1]
            return 0
        e = product[1]
        for offset, size, value in e['writes']:
            for i in range(size): self.live.bytes[offset + i] = (value >> (8 * i)) & 0xFF
        for offset, size, value in e['node']:
            self.node_bytes[offset:offset + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')
        for i, value in e['point']:
            self.point[i] = value
        if name == 'spawn':
            node, view = args[3], args[4]
            C.cast(node, P(U32))[0] = e['ret'] & MASK
            if e['ret']:
                v = C.cast(view, P(View))[0]
                base = C.addressof(self.node_e)
                v.w24 = C.cast(base + 0x24, P(U32))
                v.pos = base + 0xB0
                v.rot = base + 0xC0
                C.cast(view, P(View))[0] = v
            return 0
        if name == 'bone_count':
            C.cast(args[-1], P(C.c_uint8))[0] = e['ret'] & 0xFF
            return 0
        if name in ('request', 'cue', 'land_sound', 'w0021D490', 'bind_model', 'w00200890'):
            return 0
        C.cast(args[-1], P(I32))[0] = s32(e['ret'])
        return 0

    def run(self, adapter=False):
        """The routine through its core entry point, or (adapter) through the
        adapter a consumer binds; self.no_return / no_node mark adapters
        that do not hand the value back."""
        n, case, H = self.native, self.case, C.byref(self.host)
        A = C.byref(self.live)
        r, a, b = I32(-99), I32(-99), I32(-99)
        entry = case['entry']
        node = U32(0xDEAD)
        obj = C.addressof(self.live.bytes) + 0xB0
        blend = B(case['blend'])
        self.no_return = self.no_node = False
        if adapter:
            result = self.run_adapter(entry, A, r, a, b, node, obj, blend)
        elif entry == 'gain':
            a.value, b.value = 0x11111111, 0x22222222
            result = n.em_player_misc_001FBF50(H, obj, C.byref(a), C.byref(b), case['flat'],
                                               B(case['radius']), B(case['scale']), C.byref(r))
        elif entry in ('sound', 'sound300'):
            flat, radius = (case['flat'], B(case['radius'])) if entry == 'sound' else (0, 300.0)
            result = n.em_player_misc_001FBD50(H, obj, s32(case['id']), flat, radius, C.byref(r))
        elif entry == 'aim': result = n.em_player_misc_00182250(H, A)
        elif entry == 'ahead': result = n.em_player_misc_0017E250(H, A, obj, C.byref(r))
        elif entry == 'ahead_stack':
            result = n.em_player_misc_0017E250(H, A, C.addressof(self.vec), C.byref(r))
        elif entry == 'above': result = n.em_player_misc_0017E510(H, A, C.byref(r))
        elif entry == 'side': result = n.em_player_misc_0017E7C0(H, A, case['side'], C.byref(r))
        elif entry in ('DF70', 'DFB0', 'E0D0', 'E150', 'E1D0'):
            result = getattr(n, 'em_player_misc_0017' + entry)(H, A, case['side'], blend)
        elif entry == 'FF80': result = n.em_player_misc_0017FF80(H, A, blend)
        elif entry == 'AF0': result = n.em_player_misc_00182AF0(H, A)
        elif entry == 'depth':
            result = n.em_player_misc_00177B80(H, A, C.byref(self.ledge), B(case['y']), C.byref(r))
        elif entry == 'E650': result = n.em_player_misc_0021E650(H, A)
        elif entry == 'C1F0': result = n.em_player_misc_0015C1F0(H, A)
        elif entry == 'EFE00': result = n.em_player_misc_001EFE00(H, case['id'], A, C.byref(node))
        else: raise AssertionError(entry)
        if self.error is not None:
            raise self.error
        return result, self.products(r, a, b, node)

    def run_adapter(self, entry, A, r, a, b, node, obj, blend):
        n, case = self.native, self.case
        X = C.addressof(self.host)
        pick = case['seed'] // 2 % 3
        handle = C.c_int(-99)
        if entry == 'gain':
            a.value, b.value = 0x11111111, 0x22222222
            return n.em_player_misc_w_001FBF50(X, obj, B(case['radius']), B(case['scale']),
                                               C.byref(a), C.byref(b), C.byref(r))
        if entry == 'sound':
            self.no_return = True
            return n.em_player_misc_w_sound(X, A, s32(case['id']), case['flat'], B(case['radius']))
        if entry == 'sound300':
            if pick == 0:
                result = n.em_player_misc_w_sound_300_handle(X, A, s32(case['id']), C.byref(handle))
                r.value = handle.value
                return result
            self.no_return = True
            if pick == 1:
                return n.em_player_misc_w_sound_300(X, A, case['id'] & MASK)
            return n.em_player_misc_w_sound_300_i(X, A, s32(case['id']))
        if entry == 'aim': return n.em_player_misc_w_aim_track(X, A)
        if entry in ('ahead', 'ahead_stack', 'above', 'side', 'depth'):
            if entry == 'ahead': result = n.em_player_misc_w_ledge_ahead_self(X, A, C.byref(handle))
            elif entry == 'ahead_stack':
                result = n.em_player_misc_w_ledge_ahead(X, A, C.addressof(self.vec), C.byref(handle))
            elif entry == 'above': result = n.em_player_misc_w_ledge_above(X, A, C.byref(handle))
            elif entry == 'side':
                result = n.em_player_misc_w_ledge_side(X, A, case['side'], C.byref(handle))
            else:
                result = n.em_player_misc_w_depth(X, A, C.byref(self.ledge), B(case['y']), C.byref(handle))
            r.value = handle.value
            return result
        if entry in ('DF70', 'DFB0', 'E0D0', 'E150', 'E1D0'):
            name = {'DF70': 'DF70', 'DFB0': 'DFB0', 'E0D0': 'E0D0', 'E150': 'E150', 'E1D0': 'E1D0'}[entry]
            return getattr(n, 'em_player_misc_w_clip_' + name)(X, A, case['side'], blend)
        if entry == 'FF80': return n.em_player_misc_w_clip_FF80(X, A, blend)
        if entry == 'AF0': return n.em_player_misc_w_sound_100(X, A)
        if entry == 'E650': return n.em_player_misc_w_0021E650(X, A)
        if entry == 'C1F0': return n.em_player_misc_w_0015C1F0(X, A)
        if entry == 'EFE00':
            if pick == 0:
                self.no_node = True
                return n.em_player_misc_w_001EFE00(X, case['id'], A)
            return n.em_player_misc_w_attach(X, A, case['id'], C.byref(node))
        raise AssertionError(entry)

    def products(self, r, a, b, node):
        scratch = {}
        for name, _, count in SCRATCH:
            value = getattr(self.scratch, name)
            scratch[name] = list(value) if count > 1 else [value]
        return {'log': self.log, 'actor': bytes(self.live.bytes), 'scratch': scratch,
                'r': r.value, 'outs': (a.value, b.value), 'node': bytes(self.node_e),
                'node_value': node.value, 'no_return': self.no_return, 'no_node': self.no_node}


# ======================================================================
# One case
# ======================================================================

# The views each routine reads (it refuses without them even on a path that
# does not reach them, like a worker).
NEEDS = {'gain': ('scene', 'scratch'), 'sound': ('scene', 'scratch'), 'aim': ('scratch',),
         'ahead': ('scratch',), 'ahead_stack': ('scratch',), 'above': ('scratch',),
         'side': ('scene', 'scratch'), 'DFB0': ('scene', 'scratch'), 'AF0': ('scene', 'scratch'),
         'depth': ('scratch',), 'E650': ('scene', 'scratch'), 'C1F0': ('scene',)}
ELF = NATIVE = ORACLE = None
RETURNS = {'gain', 'sound', 'sound300', 'ahead', 'ahead_stack', 'above', 'side', 'depth'}


def compare(case, want, got, where):
    got_log = got['log']
    want_log = [entry for entry, _ in want['log']]
    assert got_log == want_log, (where, 'worker calls', want_log, got_log)
    if want['actor'] != got['actor']:
        diff = [hex(k) for k in range(0x320) if want['actor'][k] != got['actor'][k]]
        raise AssertionError((where, 'record bytes differ at', diff[:24]))
    for name, _, _ in SCRATCH:
        assert want['scratch'][name] == got['scratch'][name], (where, 'scratch', name,
                                                               want['scratch'][name], got['scratch'][name])
    entry = case['entry']
    if entry in RETURNS and not got['no_return']:
        assert want['v0'] == got['r'], (where, 'return value', want['v0'], got['r'])
    if entry == 'gain':
        assert want['outs'] == got['outs'], (where, 'gains', want['outs'], got['outs'])
    if entry == 'EFE00' and not got['no_node']:
        assert (want['v0'] & MASK) == got['node_value'], (where, 'spawned record', want['v0'], got['node_value'])
        assert want['node'] == got['node'], (where, 'spawned record bytes')


def run_case(seed):
    global ORACLE
    if ORACLE is None:
        ORACLE = Oracle(ELF)
    case = make_case(seed)
    want = ORACLE.run(case)
    where = (seed, case['entry'])
    full = Native(NATIVE, case, want)
    result, got = full.run()
    assert result == 0, (where, 'native fault', result)
    compare(case, want, got, where)
    # the same case through the adapter a consumer binds (gain: only flat 0,
    # the one 001FBF50 shape EmEffectOriginalWorkers uses)
    adapters = 0
    if seed % 2 == 1 and not (case['entry'] == 'gain' and case['flat'] != 0):
        result, got = Native(NATIVE, case, want).run(adapter=True)
        assert result == 0, (where, 'adapter fault', result)
        compare(case, want, got, where + ('adapter',))
        adapters = 1
    faults = 0
    if want['log'] and seed % 3 == 0:
        k = random.Random(seed).randrange(len(want['log']))
        cut = Native(NATIVE, case, want, fail_at=k)
        result, _ = cut.run()
        assert result == -1, (where, 'fault not reported', k)
        assert cut.calls == k + 1, (where, 'worker calls after a fault', k, cut.calls)
        faults = 1
    refusals = 0
    if seed % 7 == 0:
        called = {entry[0] for entry, _ in want['log']} | full.hit_used
        for missing in WORKER_FIELDS + ('scene', 'scratch'):
            run = Native(NATIVE, case, want, missing=missing)
            try:
                result, got = run.run()
            except AssertionError:
                result, got = -2, None
            reached = missing in called or missing in NEEDS.get(case['entry'], ())
            if result == -1:
                assert got['log'] == [] and got['actor'] == case['actor'], (where, missing, 'wrote before refusing')
                assert run.calls == 0
                refusals += 1
            else:
                assert not reached, (where, missing, 'a reached worker was missing but the routine ran')
                assert result == 0, (where, missing, result)
                compare(case, want, got, where)
    return (case['entry'], len(want['log']), faults, refusals, adapters,
            tuple(sorted(ORACLE.ee.outcomes)))


# ======================================================================
# 00122BB8 against em_random.c
# ======================================================================

def random_check(elf, native):
    """The original rand on its own state word against em_player_misc_random
    (em_random_next) drawing from em_random.c's state after em_random_seed."""
    ee = MiscEE(elf)
    block = ee.load(0x24295C)
    state_address = block + 0x58
    assert ee.load(state_address) == 1, ('the ELF image rand state is not 1', ee.load(state_address))
    lib = OUT / ('misc.dylib' if sys.platform == 'darwin' else 'misc.so')
    raw = C.CDLL(str(lib))
    raw.em_random_seed.argtypes = [U32]
    raw.em_random_seed.restype = None
    seeds = [1, 0, 0x45, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x12345678] + \
            [random.Random(9).getrandbits(32) for _ in range(reference_mode.pick(64, 8))]
    draws = reference_mode.pick(2000, 200)
    count = 0
    for seed in seeds:
        ee.save(state_address, seed)
        raw.em_random_seed(seed)
        for _ in range(draws):
            ee.r[31] = shared.RETURN
            ee.run(0x122BB8)
            want = ee.r[2] & MASK
            got, got_i = U32(0), I32(0)
            if count % 2:
                assert native.em_player_misc_random(None, C.byref(got)) == 0
                value = got.value
            else:
                assert native.em_player_misc_random_i32(None, C.byref(got_i)) == 0
                value = got_i.value & MASK
            assert value == want, ('rand differs', seed, count, value, want)
            count += 1
    assert native.em_player_misc_random(None, None) == -1
    return count


def main():
    global ELF, NATIVE
    started = time.time()
    ELF = read_elf()
    callees = check_callee_set(ELF)
    NATIVE = build_native()
    draws = random_check(ELF, NATIVE)
    total = 30000
    seeds = reference_mode.select(range(total), 2400, 0x5EED)
    results = reference_mode.parallel_map(run_case, seeds)
    outcomes, entries, faults, refusals, calls, adapters = set(), {}, 0, 0, 0, 0
    for entry, count, fault, refused, adapted, cover in results:
        adapters += adapted
        outcomes.update(cover)
        entries[entry] = entries.get(entry, 0) + 1
        faults += fault
        refusals += refused
        calls += count
    sites = branch_sites(ELF) - {a for a in branch_sites(ELF) if 0x122BB8 <= a < 0x122BB8 + 0x30}
    missing = sorted('%06X %s' % (pc, 'taken' if taken else 'not taken')
                     for pc in sites for taken in (True, False) if (pc, taken) not in outcomes)
    assert not missing, ('branch outcomes never exercised', missing)
    absent = [e for e in ENTRIES if e not in entries]
    assert not absent, ('entry points never run', absent)
    reference_mode.banner(reference_mode.part(len(seeds), total, 'cases'),
                          '%d jal targets (all hooked, inline or translated)' % callees)
    print('player misc workers vs original instructions: PASS %d cases (%s), %d worker calls '
          'identical, every one of %d conditional branches both ways, %d adapter runs, '
          '%d fault-stop cuts, %d missing-worker refusals, %d rand draws (%.1fs)' % (
              len(seeds), ', '.join('%s %d' % kv for kv in sorted(entries.items())), calls,
              len(sites), adapters, faults, refusals, draws, time.time() - started))


# ======================================================================
# World mode (EM_TEST_WORLD=1): the route beats, over the captured RAM
# ======================================================================
#
# Of the routines here, the route beats that replay the player stage alone
# reach 001FBD50 (with 001FBF50 / 001B15D0 under it) and 00122BB8; nothing on
# the route hangs from a ledge, changes the player model or runs the +7
# countdown (docs/PLAYER_MISC_WORKERS.md, "Verification"). Each beat is
# replayed twice from its source snapshot (shared.RouteReplay on FallEE):
#
# - original: 001FBD50 and 00122BB8 execute their original instructions
#   (001FB9F0 recorded, as the shared replay records it);
# - native: 001FBD50 is em_player_misc_001FBD50, its workers bound to the
#   ORIGINAL leaves in the same EE (scratch synced around every call), and
#   00122BB8 is em_player_misc_random over em_random.c's one state, seeded
#   from the snapshot's state word when the replay starts.
#
# Every frame: whole RAM and scratchpad identical (the rand state word,
# which only 00122BB8 reads and which the native keeps in em_random.c, is
# left out of the hash), the player record identical, every 001FBD50 call,
# every 001FB9F0 submit (id and both gains) and every rand draw identical.

from test_player_fall_reference import FallRoute  # noqa: E402

# beat: its frame count (the replay runs to the last trace row; the count
# only orders the parallel jobs).
WORLD_BEATS = {'05_boxes': 677, '06_hill_slide': 212, '12_crevice_jump': 345}
RAND_STATE_POINTER = 0x24295C


class WorldSound:
    """EmPlayerMiscWorkers bound to the original leaves of 001FBD50, executed
    in the same EE."""

    def __init__(self, ee, obj_address, scratch, obj):
        self.ee, self.obj_address, self.scratch, self.obj = ee, obj_address, scratch, obj
        self.error = None
        self.keep, fields = [], {}
        for name in WORKER_FIELDS:
            fn = getattr(self, 'w_' + name, None)
            fields[name] = FN[name](self.guard(fn)) if fn else FN[name]()
            self.keep.append(fields[name])
        self.workers = Workers(None, **fields)

    def guard(self, fn):
        def run(_context, *args):
            if self.error is not None:
                return -1
            try:
                return fn(*args)
            except BaseException as error:        # surfaced after the native call returns
                self.error = error
                return -1
        return run

    # native buffer -> the EE address the original passes
    def address(self, pointer):
        for name, address, n in SCRATCH:
            start = C.addressof(self.scratch) + getattr(Scratch, name).offset
            if start <= pointer < start + 4 * n:
                return address + pointer - start
        if pointer == C.addressof(self.obj):
            return self.obj_address
        for field, address in (('d810360', 0x810360), ('d8105D0', 0x8105D0)):
            if pointer == self.scene_field(field):
                return address
        raise AssertionError(('worker pointer names no original buffer', hex(pointer)))

    def scene_field(self, field):
        return C.addressof(self.scene) + getattr(Scene, field).offset

    def sync_in(self):
        for name, address, n in SCRATCH:
            value = getattr(self.scratch, name)
            for i in range(n): self.ee.save(address + 4 * i, value[i] if n > 1 else value)

    def sync_out(self):
        for name, address, n in SCRATCH:
            if n > 1:
                for i in range(n): getattr(self.scratch, name)[i] = self.ee.load(address + 4 * i)
            else:
                setattr(self.scratch, name, self.ee.load(address))

    def call(self, entry, args=(), fregs=()):
        self.sync_in()
        v0, f0 = nested_bits(self.ee, entry, args, fregs)
        self.sync_out()
        return s32(v0), f0 & MASK

    def put_f(self, out, bits):
        C.cast(out, P(U32))[0] = bits

    def w_identity(self, m): self.call(0x1029C0, (self.address(m),)); return 0
    def w_euler(self, o, m, v): self.call(0x102C58, (self.address(o), self.address(m), self.address(v))); return 0
    def w_transform(self, o, m, v): self.call(0x1026A0, (self.address(o), self.address(m), self.address(v))); return 0
    def w_vsub(self, o, a, b): self.call(0x1028D0, (self.address(o), self.address(a), self.address(b))); return 0
    def w_normalize(self, o, v): self.call(0x102760, (self.address(o), self.address(v))); return 0
    def w_dot(self, a, b, out): self.put_f(out, self.call(0x102738, (self.address(a), self.address(b)))[1]); return 0
    def w_sine(self, x, out): self.put_f(out, self.call(0x11E2A8, (), (F(x),))[1]); return 0
    def w_sqrt(self, x, out): self.put_f(out, self.call(0x11E748, (), (F(x),))[1]); return 0

    def w_side(self, a, b, yaw, out):
        C.cast(out, P(I32))[0] = self.call(0x1B1380, (self.address(a), self.address(b)), (F(yaw),))[0]
        return 0

    def w_submit(self, sound_id, a1, a2, a3, out):
        C.cast(out, P(I32))[0] = self.call(0x1FB9F0, (sound_id, a1, a2, a3))[0]
        return 0


WORLD = {}


def world_scene(ee):
    scene = Scene()
    scene.d28215B = ee.load(0x28215B, 1)
    for i in range(4):
        scene.d810360[i] = ee.load(0x810360 + 4 * i)
        scene.d8105D0[i] = ee.load(0x8105D0 + 4 * i)
    scene.d81027C = ee.load(0x81027C)
    return scene


def world_replay(job):
    beat, native_mode = job
    trace, ram, spad = WORLD[beat]
    native = WORLD['native']
    replay = FallRoute(WORLD['elf'], trace, ram, spad)
    ee = replay.ee
    state_address = ee.load(RAND_STATE_POINTER) + 0x58
    sounds, draws = [], []
    ee.hooks.pop(0x1FBD50)                  # the shared replay records it; here it runs

    def original(address):
        def hook(ee):
            saved = ee.hooks.pop(address)
            try:
                v0, f0 = nested_bits(ee, address, tuple(ee.arg(i) for i in range(4)),
                                     tuple(ee.f[12 + i] for i in range(3)))
            finally:
                ee.hooks[address] = saved
            ee.r[2], ee.f[0] = v0, f0
            return v0
        return hook

    def sound_entry(ee):
        return (replay.frame, ee.arg(0), s32(ee.arg(1)), s32(ee.arg(2)), ee.f[12] & MASK)

    if not native_mode:
        run_sound, run_rand = original(0x1FBD50), original(0x122BB8)

        def sound_hook(ee):
            entry = sound_entry(ee)
            sounds.append(entry + (s32(run_sound(ee)),))

        def rand_hook(ee):
            draws.append(run_rand(ee) & MASK)
    else:
        native.em_random_seed(ee.load(state_address))

        def sound_hook(ee):
            entry = sound_entry(ee)
            base = ee.arg(0)
            scratch, obj = Scratch(), (U32 * 4)(*[ee.load(base + 0xB0 + 4 * i) for i in range(4)])
            world = WorldSound(ee, base + 0xB0, scratch, obj)
            world.sync_out()
            world.scene = world_scene(ee)
            host = Host(C.pointer(world.workers), C.pointer(world.scene), C.pointer(scratch))
            r = I32(-99)
            result = native.em_player_misc_001FBD50(C.byref(host), C.addressof(obj), entry[2], entry[3],
                                                    B(entry[4]), C.byref(r))
            if world.error is not None:
                raise world.error
            assert result == 0, (beat, 'native fault', result)
            world.sync_in()
            ee.ret_int(r.value)
            sounds.append(entry + (r.value,))

        def rand_hook(ee):
            value = U32(0)
            assert native.em_player_misc_random(None, C.byref(value)) == 0
            draws.append(value.value)
            ee.ret_int(value.value)
    ee.hooks[0x1FBD50] = sound_hook
    ee.hooks[0x122BB8] = rand_hook
    frames, rows = [], 0
    end = trace['rows'][-1]['counter']
    while replay.counter < end:
        row = replay.step()
        mem = bytearray(ee.mem)
        mem[state_address:state_address + 4] = bytes(4)
        digest = hashlib.sha1(bytes(mem))
        digest.update(ee.spad)
        frames.append((replay.counter, digest.hexdigest(), ee.read(shared.PLAYER, 0x320),
                       len(replay.events), len(sounds), len(draws)))
        if row is not None:
            shared.route_row_check(ee, row, (beat, 'native' if native_mode else 'original', replay.counter))
            rows += 1
    return frames, replay.events, sounds, draws, rows


def world_main():
    started = time.time()
    elf = read_elf()
    beats = [b for b in os.environ.get('EM_WORLD_BEATS', ','.join(WORLD_BEATS)).split(',') if b]
    for beat in beats:
        loaded = shared.route_beat(beat)
        if isinstance(loaded, str):
            raise SystemExit('world mode: %s (docs/PLAYER_MISC_WORKERS.md)' % loaded)
        WORLD[beat] = loaded
    native = build_native()
    native.em_random_seed.argtypes = [U32]
    native.em_random_seed.restype = None
    WORLD['elf'], WORLD['native'] = elf, native
    jobs = [(beat, mode) for beat in beats for mode in (False, True)]
    results = reference_mode.parallel_map(world_replay, jobs, cost=lambda job: WORLD_BEATS[job[0]])
    by_job = dict(zip(jobs, results))
    for beat in beats:
        a_frames, a_events, a_sounds, a_draws, rows = by_job[(beat, False)]
        b_frames, b_events, b_sounds, b_draws, b_rows = by_job[(beat, True)]
        assert len(a_frames) == len(b_frames), (beat, len(a_frames), len(b_frames))
        for a, b in zip(a_frames, b_frames):
            if a[2] != b[2]:
                diff = [hex(k) for k in range(0x320) if a[2][k] != b[2][k]]
                raise AssertionError((beat, a[0], 'player bytes differ at', diff[:24]))
            assert a[3:] == b[3:], (beat, a[0], 'call counts differ', a[3:], b[3:])
            assert a[1] == b[1], (beat, a[0], 'RAM or scratchpad differs')
        assert a_sounds == b_sounds, (beat, '001FBD50 calls differ')
        assert a_events == b_events, (beat, '001FB9F0 submits / effects differ')
        assert a_draws == b_draws, (beat, 'rand draws differ')
        assert rows == b_rows and rows > 0, (beat, rows, b_rows)
        assert a_sounds and a_draws, (beat, 'the beat reached neither 001FBD50 nor 00122BB8')
        heard = sum(1 for e in a_sounds if e[-1] != -1)
        submits = sum(1 for e in a_events if e[1] == 'sound')
        print('%s: PASS %d frames identical (RAM + scratchpad + player), %d trace rows within '
              'precision, %d 001FBD50 calls (%d in range, %d submits identical), %d rand draws' % (
                  beat, len(a_frames), rows, len(a_sounds), heard, submits, len(a_draws)))
    print('player misc workers world mode: PASS %d beats (%.0fs)' % (len(beats), time.time() - started))


if __name__ == '__main__':
    if os.environ.get('EM_TEST_WORLD', '') not in ('', '0'):
        world_main()
    else:
        main()
