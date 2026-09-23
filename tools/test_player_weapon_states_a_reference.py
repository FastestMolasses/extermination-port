#!/usr/bin/env python3
"""Execute the original action machine and stance tops; compare the port.

docs/PLAYER_WEAPON_STATES_A.md. The user's pinned ELF (and, for the route
cases, the captured route RAM) supplies every instruction and table; none are
embedded here. Routines executed unmodified:

  001607D0  the action machine (+1F0)
  0016FCF0  +5 = 0x1D stance      001703E0  +5 = 0x1E stance
  001729A0  +5 = 0x1F stance
  and the two byte-matched leaves they call, copy_qw4 (00102958) and
  001031E0, which the translation performs in place.

Every other callee is hooked, scripted per case and recorded (never
simulated as a claim about the callee); the native module
(src/game/em_player_weapon_states_a.c) gets the same script through its
workers. The test asserts that the hooked set is exactly the set of jal
targets of these routines apart from the two leaves, so no callee runs
unhooked. The table / node / object words the original loads directly
(D_00248B88 / D_00248C68, *(D_00275B40 + 0x10) + 0x90.., *(p+20) + C0/C8)
reach the native side through its data workers, read from the same memory.

Arithmetic: WeaponEE is the fall oracle's FallEE (tools/
test_player_fall_reference.py), which routes every COP1 op and VU0 macro op
through tools/ee_float_model.py (the measured model); the shared files are
not edited.

Compared per case: all 0x320 record bytes, D_008106E0, D_00810CA4 and
0x70003A20, the return value of 001607D0, and the worker call sequence with
every argument (floats as bits). Every store the original instructions make
must land in the record, D_008106E0, 0x70003A20 or the stack (asserted).

Default run (~10 s): a fixed-seed sample of the synthetic cases plus a
covering sample of the captured-state cases, with every conditional branch
of the four routines asserted both ways, fault-stop cuts and the
missing-worker refusals. EM_TEST_FULL=1: every case.

Captured-state cases: the player record (0x8102B0), the pad configuration
words (0x70003B74..7E), D_00810C61, D_00810CA4, D_008106E0 and the object
+20 points to, from each of the 15 route captures
(../Extermination/build/s87/route/*/eeMemory.bin + scratchpad.bin). No
route beat reaches +5 = 0x1D..0x22, so +1F0 / +5 / +6 and the pad words are
set per case over the captured record; the bone node D_00275B40 slot 4 is
synthetic there too (the captured D_00275B40 belongs to whichever actor ran
last in the frame).
"""
import ctypes as C
import os
import random
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode  # noqa: E402
import test_player_slide_reference as shared  # noqa: E402
from test_player_slide_reference import EE, read_elf, s32, sx32  # noqa: E402
from test_player_fall_reference import FallEE  # noqa: E402

MASK = 0xFFFFFFFF
LANE = os.environ.get('EM_LANE', 'b6-player-weapon-states-a')
OUT = ROOT / 'build' / LANE
ROUTE = shared.DECOMP / 'build/s87/route'


# ======================================================================
# The routines, their callees and their globals
# ======================================================================

ACTION, TOP1D, TOP1E, TOP1F = 0x1607D0, 0x16FCF0, 0x1703E0, 0x1729A0
SIZES = {ACTION: 0x850, TOP1D: 0x6EC, TOP1E: 0x678, TOP1F: 0x658}
ENTRY = {'action': ACTION, '1D': TOP1D, '1E': TOP1E, '1F': TOP1F}
LEAVES = {0x102958: 'copy_qw4', 0x1031E0: '001031E0'}   # translated in place, run unhooked

CALLEES = {
    0x16F5D0: 'w0016F5D0', 0x17A8B0: 'w0017A8B0', 0x17A970: 'w0017A970', 0x17AAD0: 'w0017AAD0',
    0x17C370: 'w0017C370', 0x17B300: 'w0017B300', 0x16F530: 'w0016F530', 0x1749A0: 'request',
    0x1C6DA0: 'skeleton', 0x17A130: 'matrix', 0x17ABA0: 'w0017ABA0', 0x185A10: 'w00185A10',
    0x185E30: 'w00185E30', 0x199220: 'w00199220', 0x170A60: 'w00170A60', 0x171320: 'w00171320',
    0x171670: 'w00171670', 0x171B00: 'w00171B00', 0x171E90: 'w00171E90', 0x1723D0: 'w001723D0',
    0x172860: 'w00172860', 0x16F600: 'w0016F600', 0x11E620: 'atan2', 0x1B1470: 'wrap',
    0x1B12B0: 'approach', 0x1FBD50: 'sound', 0x174AC0: 'heading', 0x17C440: 'reentry',
    0x17C540: 'handoff', 0x178B90: 'translate', 0x1764E0: 'probes', 0x175900: 'floor',
    0x1796C0: 'fall_check',
}

SPAD_MASKS = (0x70003B74, 0x70003B76, 0x70003B78, 0x70003B7C, 0x70003B7E)
D_E70, D_E74, D_C61, D_E0, D_CA4, S_3A20, D_B40 = (
    0x810E70, 0x810E74, 0x810C61, 0x8106E0, 0x810CA4, 0x70003A20, 0x275B40)
PLAYER = 0x8102B0
ACTOR = 0x680000                                     # unit cases
NODE_TABLE, NODE, TARGET = 0x1F80000, 0x1F81000, 0x1F82000   # synthetic, both modes


def in_translated(pc):
    return any(start <= pc < start + size for start, size in SIZES.items())


def check_callee_set(elf):
    ee = EE(elf)
    targets = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            if word >> 26 == 3:
                targets.add((word & 0x3FFFFFF) << 2)
    missing = sorted(t for t in targets if t not in CALLEES and t not in LEAVES)
    assert not missing, ('callees neither hooked nor translated', [hex(t) for t in missing])
    unused = sorted(t for t in list(CALLEES) + list(LEAVES) if t not in targets)
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
                continue                                  # b: unconditional
            if ee.branch(word, pc) is not None:
                sites.add(pc)
    return sites


class WeaponEE(FallEE):
    """FallEE (COP1/VU0 through ee_float_model) recording branch outcomes
    inside the four routines and every store the instructions make."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.outcomes = set()
        self.track = None
        self.in_hook = False

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and in_translated(pc):
            self.outcomes.add((pc, b[0]))
        return b

    def save(self, address, value, size=4):
        if self.track is not None and not self.in_hook:
            self.track.append((address & MASK, size))
        super().save(address, value, size)


# ======================================================================
# Native side (ctypes)
# ======================================================================

class LiveActor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', C.c_void_p), ('link_prev', C.c_void_p),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


class Scene(C.Structure):
    _fields_ = [('spad3B74', C.c_uint16), ('spad3B76', C.c_uint16), ('spad3B78', C.c_uint16),
                ('spad3B7C', C.c_uint16), ('spad3B7E', C.c_uint16), ('d810E70', C.c_uint16),
                ('d810E74', C.c_uint16), ('d810C61', C.c_uint8), ('d8106E0', C.c_uint32),
                ('d810CA4', C.c_uint8), ('spad3A20', C.c_uint32)]


P = C.POINTER
LA = P(LiveActor)
I, U32, FLT, VP = C.c_int, C.c_uint32, C.c_float, C.c_void_p
FN = {
    'actor': C.CFUNCTYPE(I, VP, LA),
    'actor_int': C.CFUNCTYPE(I, VP, LA, I),
    'actor_int_result': C.CFUNCTYPE(I, VP, LA, I, P(I)),
    'actor_result': C.CFUNCTYPE(I, VP, LA, P(I)),
    'request': C.CFUNCTYPE(I, VP, LA, I, I, FLT),
    'clip_id': C.CFUNCTYPE(I, VP, U32, C.c_uint, P(C.c_int16)),
    'bone': C.CFUNCTYPE(I, VP, C.c_uint, P(U32)),
    'lock': C.CFUNCTYPE(I, VP, LA, U32, P(U32)),
    'actor_float': C.CFUNCTYPE(I, VP, LA, FLT),
    'link20': C.CFUNCTYPE(I, VP, U32, P(U32), P(U32)),
    'atan2': C.CFUNCTYPE(I, VP, U32, U32, P(U32)),
    'wrap': C.CFUNCTYPE(I, VP, U32, P(U32)),
    'approach': C.CFUNCTYPE(I, VP, U32, U32, U32, P(U32)),
    'sound': C.CFUNCTYPE(I, VP, LA, I, I, FLT),
    'floor': C.CFUNCTYPE(I, VP, LA, I, P(I)),
}
# EmPlayerWeaponWorkers, in header order.
WORKER_FIELDS = (
    ('w0016F5D0', 'actor'), ('w0017A8B0', 'actor_int_result'), ('w0017A970', 'actor_int_result'),
    ('w0017AAD0', 'actor_result'), ('w0017C370', 'actor'), ('w0017B300', 'actor_int'),
    ('w0016F530', 'actor_int'), ('request', 'request'), ('clip_id', 'clip_id'), ('skeleton', 'actor'),
    ('matrix', 'actor'), ('bone', 'bone'), ('w0017ABA0', 'actor'), ('w00185A10', 'lock'),
    ('w00185E30', 'lock'), ('w00199220', 'actor'), ('w00170A60', 'actor_int'), ('w00171320', 'actor'),
    ('w00171670', 'actor'), ('w00171B00', 'actor'), ('w00171E90', 'actor'), ('w001723D0', 'actor'),
    ('w00172860', 'actor_float'), ('w0016F600', 'actor'), ('link20', 'link20'), ('atan2', 'atan2'),
    ('wrap', 'wrap'), ('approach', 'approach'), ('sound', 'sound'), ('heading', 'actor_int'),
    ('reentry', 'actor_int'), ('handoff', 'actor'), ('translate', 'actor_int'), ('probes', 'actor'),
    ('floor', 'floor'), ('fall_check', 'actor'),
)
DATA_WORKERS = ('clip_id', 'bone', 'link20')   # loads in the original, not calls


class Workers(C.Structure):
    _fields_ = [('context', VP)] + [(name, FN[kind]) for name, kind in WORKER_FIELDS]


class States(C.Structure):
    _fields_ = [('workers', P(Workers)), ('scene', P(Scene))]


# The workers each entry point can reach (em_player_weapon_*_bound).
STANCE = {'w0017B300', 'w0016F530', 'request', 'clip_id', 'skeleton', 'matrix', 'bone', 'w0017ABA0',
          'w00170A60', 'w00171320', 'w00171670', 'w00171B00', 'w00171E90', 'w001723D0', 'w0016F600',
          'link20', 'atan2', 'wrap', 'approach', 'sound'}
TAIL = {'heading', 'reentry', 'handoff', 'translate', 'probes', 'floor', 'fall_check'}
REACH = {
    'action': {'w0016F5D0', 'w0017A8B0', 'w0017A970', 'w0017AAD0', 'w0017C370'},
    '1D': STANCE | TAIL | {'w00185A10', 'w00185E30'},
    '1E': STANCE | TAIL | {'w00199220'},
    '1F': STANCE | {'w00185A10', 'w00185E30', 'w00172860'},
}


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('weapon_states_a.dylib' if sys.platform == 'darwin' else 'weapon_states_a.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'src/game/em_player_weapon_states_a.c', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    S = P(States)
    for name in ('em_player_weapon_state1D', 'em_player_weapon_state1E', 'em_player_weapon_state1F'):
        getattr(native, name).argtypes = [S, LA]
    native.em_player_weapon_001607D0.argtypes = [S, LA, P(I)]
    return native


# ======================================================================
# Scripted callee effects (identical on both sides)
# ======================================================================

def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


FLAGS = (0, 0x1000, 0x8000, 0x9000, 0x1200)
# Record fields a callee may change before the routine reads them again.
AFTER = ((0x302, 1, (0, 0, 1)), (0x275, 1, (0, 0, 1, 2, 3, 4, 5, 6)), (0x274, 1, (0, 1)),
         (0x1F0, 1, (0x33, 0x32, 0x35, 0x31, 0x34, 0)), (0x200, 4, FLAGS), (0x23F, 1, (0, 1, 2, 3)),
         (6, 1, (0, 1, 2, 0x62, 0x63, 0x6D, 0xFF)), (0x2F2, 1, (0, 1)), (0x2F0, 1, (0, 1, 2, 0xFF)),
         (5, 1, (0x1D, 0x1E, 0x1F, 0x20)), (0x28, 2, (0, 1, 0xFFFF)), (0x317, 1, (0, 1)))
GLOBAL_WRITERS = ('w0017ABA0', 'w00185A10', 'w00185E30', 'w00199220', 'w0017B300', 'w0016F530')
INT_RESULT = ('w0017A8B0', 'w0017A970', 'w0017AAD0', 'w00185A10', 'w00185E30', 'floor')
FLOAT_RESULT = ('atan2', 'wrap', 'approach')


def random_float(rng):
    choice = rng.random()
    if choice < 0.7:
        return F(rng.uniform(-4.0, 4.0))
    if choice < 0.9:
        return F(rng.choice((0.0, -0.0, 3.14159274, -3.14159274, 1.5707964, 1e-39)))
    return rng.getrandbits(32)


def effect_for(rng, name):
    e = {'ret': 0, 'fret': None, 'writes': [], 'globals': [], 'node': None}
    if rng.random() < 0.35:
        for _ in range(rng.choice((1, 1, 2))):
            offset, size, values = rng.choice(AFTER)
            e['writes'].append((offset, size, rng.choice(values)))
    if name == 'w0017ABA0' and rng.random() < 0.3:
        e['writes'].append((0x302, 1, 1))                  # the aim steer flags +302
    if name in GLOBAL_WRITERS:
        if rng.random() < 0.3:
            e['globals'].append((D_E0, 4, rng.choice((0, 0, 0x7AB730, rng.getrandbits(24)))))
        if rng.random() < 0.3:
            e['globals'].append((D_CA4, 1, rng.choice((0, 0, 1, 2, 0xFF))))
    if name in ('skeleton', 'matrix') and rng.random() < 0.7:
        e['node'] = [random_float(rng) for _ in range(16)]
    if name in ('w0017A8B0', 'w0017A970'):
        e['ret'] = rng.choice((0, 1, 1, 2, -5 & MASK))
    elif name == 'w0017AAD0':
        e['ret'] = rng.choice((0, 0, 1, 7))
    elif name in ('w00185A10', 'w00185E30'):
        e['ret'] = rng.choice((0, 0x7AB730, 0x823FF0, rng.getrandbits(24)))
    elif name == 'floor':
        e['ret'] = rng.choice((0, 1, 0x81))
    elif name in FLOAT_RESULT:
        e['fret'] = random_float(rng)
    return e


class Script:
    def __init__(self, seed, fail_at=None):
        self.seed, self.count, self.fail_at = seed, 0, fail_at

    def next(self, name):
        rng = random.Random('%s:%d:%s' % (self.seed, self.count, name))
        index = self.count
        self.count += 1
        return index, effect_for(rng, name)


# ======================================================================
# Cases
# ======================================================================

ACTION_MODES = (0, 1, 2, 3, 4, 5, 6, 7, 8, 0x26, 0x27, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
                0x36, 0x37, 0x38, 0xFF)
TOP_STATES = (0, 1, 2, 3, 4, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x6E, 0x6F, 0x70)
CAPTURED_MASKS = None   # filled from the first route capture (the default configuration)


def put(actor, offset, size, value):
    actor[offset:offset + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')


def scene_for(rng, masks=None):
    if masks is None:
        if rng.random() < 0.8:
            bits = rng.sample([1 << i for i in range(16)], 5)
        else:
            bits = [rng.choice((0, 1, 2, 8, 0x20, 0xFFFF)) for _ in range(5)]
        masks = bits
    pick = lambda: sum(m for m in masks if rng.random() < 0.3) | (rng.getrandbits(16) if rng.random() < 0.1 else 0)
    return {'masks': list(masks), 'e70': pick() & 0xFFFF, 'e74': pick() & 0xFFFF,
            'c61': rng.choice((0, 0, 1, 2)), 'e0': rng.choice((0, 0, 0x7AB730, rng.getrandbits(24))),
            'ca4': rng.choice((0, 0, 1, 2, 0xFF)), 's3a20': rng.getrandbits(32)}


def stance_fields(rng, actor, top, wild):
    put(actor, 4, 1, 1)
    put(actor, 5, 1, rng.choice(({'1D': 0x1D, '1E': 0x1E, '1F': 0x1F}[top],) * 4 +
                                (0x1D, 0x1E, 0x1E, 0x1F, 0x20, 0x20, 7)))
    put(actor, 6, 1, rng.choice(TOP_STATES + (0, 1, 2, 3, 0x63, 0x64, 0x66)))
    put(actor, 0x275, 1, rng.choice((0, 0, 1, 2, 3, 4, 5, 6, 0xFF)))
    put(actor, 0x274, 1, rng.choice((0, 1)))
    put(actor, 0x2F0, 1, rng.choice((0, 1, 2, 3, 0xFF)))
    put(actor, 0x2F2, 1, rng.choice((0, 1)))
    put(actor, 0x302, 1, rng.choice((0, 0, 1)))
    put(actor, 0x317, 1, rng.choice((0, 1)))
    put(actor, 0x23F, 1, rng.choice((0, 1, 2, 3)))
    put(actor, 0x1F0, 1, rng.choice((0x33, 0x32, 0x35, 0x31, 0x34, 0)))
    put(actor, 0x200, 4, rng.choice(FLAGS) | (rng.getrandbits(8) if rng.random() < 0.2 else 0))
    put(actor, 0x28, 2, rng.choice((0, 1, 2, 4, 8, 0xFFFF, 0x8000)))
    if not wild:
        choose = lambda values: F(rng.choice(values))
        put(actor, 0x3C, 4, choose((4.0, 3.9999998, 4.0000005, 0.0, -1.0, 10.0, 12.5)))
        put(actor, 0x27C, 4, F(rng.uniform(0, 1)))
        put(actor, 0x278, 4, F(rng.uniform(0, 1)))
        for off in (0x218, 0xC4):
            put(actor, off, 4, F(rng.uniform(-3.2, 3.2)))
        for off in (0xB0, 0xB4, 0xB8, 0x294, 0x26C, 0x270):
            put(actor, off, 4, F(rng.uniform(-400, 400)))


def make_case(seed):
    rng = random.Random(seed)
    entry = rng.choice(('action',) * 4 + ('1D',) * 3 + ('1E',) * 3 + ('1F',) * 3)
    actor = bytearray(rng.getrandbits(8) for _ in range(0x320))
    wild = rng.random() < 0.08
    if entry == 'action':
        put(actor, 0x1F0, 1, rng.choice(ACTION_MODES + (0, 1, 0x27) + (0x31, 0x32, 0x34, 0x35) * 4))
        put(actor, 0x236, 1, rng.choice((0, 0, 0, 1)))
    else:
        stance_fields(rng, actor, entry, wild)
    put(actor, 0x20, 4, TARGET)
    return {'seed': seed, 'entry': entry, 'base': ACTOR, 'actor': bytes(actor),
            'scene': scene_for(rng), 'node': [random_float(rng) for _ in range(16)],
            'target': (random_float(rng), random_float(rng))}


# ======================================================================
# The original side
# ======================================================================

class Oracle:
    """The original routines on a WeaponEE with every callee hooked."""

    def __init__(self, elf, ram=None, spad=None):
        self.ee = WeaponEE(elf, ram, spad)
        for address, name in CALLEES.items():
            self.ee.hooks[address] = self.hook(name)
        self.base = ACTOR

    def hook(self, name):
        def run(ee):
            ee.in_hook = True
            try:
                self.log.append(self.log_entry(name, ee))
                index, e = self.script.next(name)
                for offset, size, value in e['writes']:
                    ee.save(self.base + offset, value, size)
                for address, size, value in e['globals']:
                    ee.save(address, value, size)
                if e['node'] is not None:
                    for i, value in enumerate(e['node']):
                        ee.save(NODE + 0x90 + 4 * i, value)
                if e['fret'] is not None:
                    ee.f[0] = e['fret']
                ee.ret_int(e['ret'])
            finally:
                ee.in_hook = False
        return run

    def log_entry(self, name, ee):
        a = ee.arg
        f = lambda n: ee.f[12 + n] & MASK
        if name not in ('atan2', 'wrap', 'approach'):
            assert a(0) == self.base, (name, hex(a(0)))
        if name in ('w0017A8B0', 'w0017A970', 'w0017B300', 'w0016F530', 'w00170A60', 'heading',
                    'reentry', 'translate', 'floor'):
            return (name, s32(a(1)))
        if name in ('request', 'sound'): return (name, s32(a(1)), s32(a(2)), f(0))
        if name == 'w00172860': return (name, f(0))
        if name in ('w00185A10', 'w00185E30'): return (name, a(1))
        if name == 'atan2': return (name, f(0), f(1))
        if name == 'wrap': return (name, f(0))
        if name == 'approach': return (name, f(0), f(1), f(2))
        if name == 'probes':
            assert ee.r[17] & MASK == self.base, ('001764E0 $s1', hex(ee.r[17]))
        return (name,)

    def setup(self, case):
        ee = self.ee
        self.base = case['base']
        ee.r, ee.rh = [0] * 32, [0] * 32
        ee.f, ee.acc, ee.cond = [0] * 32, 0, False
        ee.vacc, ee.q = [0, 0, 0, 0], 0
        ee.r[28], ee.r[29], ee.r[17] = 0x27D370, shared.STACK_TOP, 0x5A5A0004
        ee.write(self.base, case['actor'])
        s = case['scene']
        for address, value in zip(SPAD_MASKS, s['masks']): ee.save(address, value, 2)
        ee.save(D_E70, s['e70'], 2); ee.save(D_E74, s['e74'], 2); ee.save(D_C61, s['c61'], 1)
        ee.save(D_E0, s['e0']); ee.save(D_CA4, s['ca4'], 1); ee.save(S_3A20, s['s3a20'])
        ee.save(D_B40, NODE_TABLE); ee.save(NODE_TABLE + 0x10, NODE)
        for i, value in enumerate(case['node']): ee.save(NODE + 0x90 + 4 * i, value)
        target = int.from_bytes(case['actor'][0x20:0x24], 'little')
        if target == TARGET:
            ee.save(TARGET + 0xC0, case['target'][0]); ee.save(TARGET + 0xC8, case['target'][1])

    def run(self, case, script):
        ee = self.ee
        self.setup(case)
        self.script, self.log = script, []
        ee.track = []
        ee.call(ENTRY[case['entry']], (self.base,))
        writes, ee.track = ee.track, None
        for address, size in writes:
            ok = (self.base <= address and address + size <= self.base + 0x320) or \
                 (address, size) in ((D_E0, 4), (S_3A20, 4)) or 0x7F000000 <= address < 0x7F100000
            assert ok, (case['seed'], 'original store outside the compared set', hex(address), size)
        return {'actor': ee.read(self.base, 0x320), 'log': self.log,
                'globals': (ee.load(D_E0), ee.load(D_CA4, 1), ee.load(S_3A20)),
                'v0': s32(ee.r[2]) if case['entry'] == 'action' else None}

    def data(self, case):
        """What the native data workers serve: the clip tables (read-only,
        from this memory), the object words +C0/+C8 at the case start."""
        target = int.from_bytes(case['actor'][0x20:0x24], 'little')
        self.setup(case)
        return {'target_word': target,
                'target': (self.ee.load(target + 0xC0), self.ee.load(target + 0xC8)),
                'load': self.ee.load}


# ======================================================================
# The native side
# ======================================================================

class NativeRun:
    """em_player_weapon_states_a.c with Python workers replaying the script."""

    def __init__(self, native, case, script, data, missing=None):
        self.native, self.case, self.script, self.data, self.log = native, case, script, data, []
        self.live = LiveActor()
        C.memmove(self.live.bytes, case['actor'], 0x320)
        s = case['scene']
        self.scene = Scene(*s['masks'], s['e70'], s['e74'], s['c61'], s['e0'], s['ca4'], s['s3a20'])
        self.node = list(case['node'])
        self.keep, fields = [], {}
        for field, kind in WORKER_FIELDS:
            fields[field] = FN[kind](self.worker(field)) if field != missing else FN[kind]()
            self.keep.append(fields[field])
        self.workers = Workers(None, **fields)
        self.states = States(C.pointer(self.workers), C.pointer(self.scene))
        if missing == 'scene':
            self.states.scene = P(Scene)()
        if missing == 'workers':
            self.states.workers = P(Workers)()

    def call(self, name, entry):
        self.log.append(entry)
        index, e = self.script.next(name)
        if self.script.fail_at == index:
            return None
        for offset, size, value in e['writes']:
            for i in range(size): self.live.bytes[offset + i] = (value >> (8 * i)) & 0xFF
        for address, size, value in e['globals']:
            if address == D_E0: self.scene.d8106E0 = value
            elif address == D_CA4: self.scene.d810CA4 = value
            else: raise AssertionError(hex(address))
        if e['node'] is not None: self.node = list(e['node'])
        return e

    def worker(self, field):
        fb = lambda value: struct.unpack('<I', struct.pack('<f', value))[0]
        plain = lambda name: (lambda _, a: -1 if self.call(name, (name,)) is None else 0)
        with_int = lambda name: (lambda _, a, x: -1 if self.call(name, (name, x)) is None else 0)

        if field in ('w0016F5D0', 'w0017C370', 'skeleton', 'matrix', 'w0017ABA0', 'w00199220',
                     'w00171320', 'w00171670', 'w00171B00', 'w00171E90', 'w001723D0', 'w0016F600',
                     'handoff', 'probes', 'fall_check'):
            return plain(field)
        if field in ('w0017B300', 'w0016F530', 'w00170A60', 'heading', 'reentry', 'translate'):
            return with_int(field)
        if field in ('w0017A8B0', 'w0017A970', 'floor'):
            def fn(_, a, x, out):
                e = self.call(field, (field, x))
                if e is None: return -1
                out[0] = s32(e['ret'])
                return 0
            return fn
        if field == 'w0017AAD0':
            def fn(_, a, out):
                e = self.call(field, (field,))
                if e is None: return -1
                out[0] = s32(e['ret'])
                return 0
            return fn
        if field in ('w00185A10', 'w00185E30'):
            def fn(_, a, current, out):
                e = self.call(field, (field, current))
                if e is None: return -1
                out[0] = e['ret'] & MASK
                return 0
            return fn
        if field in ('request', 'sound'):
            return lambda _, a, x, y, radius: (
                -1 if self.call(field, (field, x, y, fb(radius))) is None else 0)
        if field == 'w00172860':
            return lambda _, a, rate: -1 if self.call(field, (field, fb(rate))) is None else 0
        if field in FLOAT_RESULT:
            def fn(_, *args):
                e = self.call(field, (field,) + tuple(args[:-1]))
                if e is None: return -1
                args[-1][0] = e['fret']
                return 0
            return fn
        if field == 'clip_id':
            def fn(_, table, index, out):
                raw = self.data['load'](table + 2 * index, 2)
                out[0] = raw - 0x10000 if raw & 0x8000 else raw
                return 0
            return fn
        if field == 'bone':
            def fn(_, slot, out):
                assert slot == 4, slot
                for i in range(16): out[i] = self.node[i]
                return 0
            return fn
        if field == 'link20':
            def fn(_, word, c0, c8):
                assert word == self.data['target_word'], (hex(word), hex(self.data['target_word']))
                c0[0], c8[0] = self.data['target']
                return 0
            return fn
        raise AssertionError(field)

    def run(self):
        n, entry = self.native, self.case['entry']
        S, A = C.byref(self.states), C.byref(self.live)
        v0 = None
        if entry == 'action':
            out = C.c_int(-99)
            result = n.em_player_weapon_001607D0(S, A, C.byref(out))
            v0 = out.value
        else:
            result = getattr(n, 'em_player_weapon_state' + entry)(S, A)
        return result, {'actor': bytes(self.live.bytes), 'log': self.log,
                        'globals': (self.scene.d8106E0, self.scene.d810CA4, self.scene.spad3A20),
                        'v0': v0}


# ======================================================================
# Running a case
# ======================================================================

ELF = NATIVE = None
ORACLES = {}


def oracle_for(key):
    if key not in ORACLES:
        if key == 'unit':
            ORACLES[key] = Oracle(ELF)
        else:
            ram = (ROUTE / key / 'eeMemory.bin').read_bytes()
            spad = (ROUTE / key / 'scratchpad.bin').read_bytes()
            ORACLES[key] = Oracle(ELF, ram, spad)
    return ORACLES[key]


def compare(oracle, case, seed_key, cut):
    data = oracle.data(case)
    want = oracle.run(case, Script(seed_key))
    result, got = NativeRun(NATIVE, case, Script(seed_key), data).run()
    where = (seed_key, case['entry'], hex(case['actor'][6]), hex(case['actor'][0x1F0]))
    assert result == 0, (where, 'native fault', result)
    assert want['log'] == got['log'], (where, 'worker calls', want['log'], got['log'])
    if want['actor'] != got['actor']:
        diff = [hex(k) for k in range(0x320) if want['actor'][k] != got['actor'][k]]
        raise AssertionError((where, 'record bytes differ at', diff[:24]))
    assert want['globals'] == got['globals'], (where, 'globals', want['globals'], got['globals'])
    assert want['v0'] == got['v0'], (where, 'return', want['v0'], got['v0'])
    faults = 0
    if cut and want['log']:
        k = random.Random(str(seed_key)).randrange(len(want['log']))
        result, cutoff = NativeRun(NATIVE, case, Script(seed_key, fail_at=k), data).run()
        assert result == -1, (where, 'fault not reported', k)
        assert cutoff['log'] == want['log'][:k + 1], (where, 'calls after a fault', k)
        faults = 1
    return len(want['log']), faults


def run_unit(seed):
    oracle = oracle_for('unit')
    case = make_case(seed)
    calls, faults = compare(oracle, case, seed, seed % 5 == 0)
    return case['entry'], calls, faults, tuple(sorted(oracle.ee.outcomes))


# ---- captured route states ------------------------------------------------

def route_states():
    if not ROUTE.exists():
        return []
    return sorted(p.name for p in ROUTE.iterdir()
                  if (p / 'eeMemory.bin').exists() and (p / 'scratchpad.bin').exists())


def route_cases(state):
    """The case list over one captured state (deterministic), read from the
    capture files, not from an oracle's (mutable) memory."""
    with open(ROUTE / state / 'eeMemory.bin', 'rb') as f:
        f.seek(PLAYER)
        ram_player = f.read(0x320)
        word = lambda address, size=4: (f.seek(address), int.from_bytes(f.read(size), 'little'))[1]
        globals_ = {'e70': word(D_E70, 2), 'e74': word(D_E74, 2), 'c61': word(D_C61, 1),
                    'e0': word(D_E0), 'ca4': word(D_CA4, 1)}
    spad = (ROUTE / state / 'scratchpad.bin').read_bytes()
    masks = [int.from_bytes(spad[a - 0x70000000:a - 0x70000000 + 2], 'little') for a in SPAD_MASKS]
    base_scene = dict(globals_, masks=masks,
                      s3a20=int.from_bytes(spad[0x3A20:0x3A24], 'little'))
    rng = random.Random('route:' + state)
    node = [F(rng.uniform(-400, 400)) for _ in range(16)]
    cases = []
    pads = [(0, 0)]
    for m in masks:
        pads += [(m, 0), (0, m), (m, m)]
    pads += [(masks[3] | masks[4], 0), (masks[3], masks[2]), (masks[4], masks[4] | masks[1])]
    for mode in ACTION_MODES:
        for held, pressed in pads:
            for c61 in (0, 1):
                actor = bytearray(ram_player)
                put(actor, 0x1F0, 1, mode)
                cases.append({'entry': 'action', 'base': PLAYER, 'actor': bytes(actor),
                              'scene': dict(base_scene, e70=held, e74=pressed, c61=c61),
                              'node': node, 'target': None, 'key': (state, 'action', mode, held, pressed, c61)})
    for top in ('1D', '1E', '1F'):
        for st in TOP_STATES:
            for variant in range(6):
                v = random.Random('%s:%s:%d:%d' % (state, top, st, variant))
                actor = bytearray(ram_player)
                put(actor, 5, 1, {'1D': 0x1D, '1E': 0x1E, '1F': 0x1F}[top] if variant < 4 else 0x20)
                put(actor, 6, 1, st)
                put(actor, 0x200, 4, (ram_player[0x200] & ~0x9000) | v.choice(FLAGS))
                put(actor, 0x317, 1, variant & 1)
                put(actor, 0x275, 1, v.choice((0, 1, 2, 3, 4, 5)) if variant >= 2 else 0)
                put(actor, 0x274, 1, variant >> 1 & 1)
                put(actor, 0x1F0, 1, v.choice((0x31, 0x32, 0x33, 0x35)))
                put(actor, 0x28, 2, v.choice((0, 4, 8)))
                put(actor, 0x3C, 4, F(v.choice((4.0, 4.0000005, 0.0))))
                scene = dict(base_scene, ca4=(base_scene['ca4'], 0, 1)[variant % 3],
                             e0=(0, 0x7AB730)[variant >> 2 & 1])
                cases.append({'entry': top, 'base': PLAYER, 'actor': bytes(actor), 'scene': scene,
                              'node': node, 'target': None, 'key': (state, top, st, variant)})
    return cases


def run_route_state(item):
    state, indices = item
    oracle = oracle_for(state)
    ee = oracle.ee
    keep = {address: ee.read(address, size) for address, size in
            ((PLAYER, 0x320), (D_E0, 4), (D_CA4, 1), (S_3A20, 4), (D_E70, 8), (D_C61, 1),
             (0x70003B74, 12), (D_B40, 4), (NODE_TABLE, 0x20), (NODE + 0x90, 0x40))}
    cases = route_cases(state)
    calls = faults = 0
    for i in indices:
        case = cases[i]
        for address, data in keep.items(): ee.write(address, data)
        count, fault = compare(oracle, case, '%s:%d' % (state, i), i % 7 == 0)
        calls += count
        faults += fault
    return state, len(indices), calls, faults, tuple(sorted(oracle.ee.outcomes))


# ---- missing workers --------------------------------------------------------

def missing_worker_checks(native):
    """A reachable worker (or the scene / worker table) missing: the entry
    point returns -1 before any write and calls nothing. An unreachable one
    missing: the routine runs as when fully bound."""
    count = 0
    fields = [f for f, _ in WORKER_FIELDS] + ['scene', 'workers']
    oracle = oracle_for('unit')
    for entry in ENTRY:
        seed = next(s for s in range(5000, 9000) if make_case(s)['entry'] == entry)
        case = make_case(seed)
        data = oracle.data(case)
        full, ref = NativeRun(native, case, Script(seed), data).run()
        assert full == 0, (entry, full)
        for field in fields:
            result, got = NativeRun(native, case, Script(seed), data, missing=field).run()
            if field in ('scene', 'workers') or field in REACH[entry]:
                assert result == -1 and got['log'] == [] and got['actor'] == case['actor'], (field, entry)
                s = case['scene']
                assert got['globals'] == (s['e0'], s['ca4'], s['s3a20']), (field, entry, 'globals')
            else:
                assert result == 0 and got == ref, (field, entry, 'unreachable worker changed the run')
            count += 1
    return count


def main():
    global ELF, NATIVE
    started = time.time()
    ELF = read_elf()
    callees = check_callee_set(ELF)
    NATIVE = build_native()

    total = 120000
    seeds = reference_mode.select(range(total), 30000, 0x5EED)
    unit = reference_mode.parallel_map(run_unit, seeds)
    outcomes, entries, faults, calls = set(), {}, 0, 0
    for entry, count, fault, cover in unit:
        outcomes.update(cover)
        entries[entry] = entries.get(entry, 0) + 1
        faults += fault
        calls += count

    states = route_states()
    route_total = route_run = route_calls = 0
    if states:
        items = []
        for state in states:
            cases = route_cases(state)
            chosen = reference_mode.select(range(len(cases)), 40, zlib.crc32(state.encode()),
                                           axes=(lambda i, c=cases: c[i]['entry'],
                                                 lambda i, c=cases: c[i]['key'][2]))
            route_total += len(cases)
            items.append((state, chosen))
        for state, count, n_calls, n_faults, cover in reference_mode.parallel_map(
                run_route_state, items, cost=lambda item: len(item[1])):
            outcomes.update(cover)
            route_run += count
            route_calls += n_calls
            faults += n_faults

    sites = branch_sites(ELF)
    missing = sorted('%06X %s' % (pc, 'taken' if taken else 'not taken')
                     for pc in sites for taken in (True, False) if (pc, taken) not in outcomes)
    assert not missing, ('branch outcomes never exercised', missing)
    absent = [e for e in ENTRY if e not in entries]
    assert not absent, ('entry points never run', absent)
    refusals = missing_worker_checks(NATIVE)
    reference_mode.banner(reference_mode.part(len(seeds), total, 'synthetic cases'),
                          reference_mode.part(route_run, route_total, 'captured-state cases') +
                          ' over %d route captures' % len(states),
                          '%d jal targets (all hooked or translated)' % callees)
    print('player weapon states A vs original instructions: PASS %d synthetic cases (%s), '
          '%d captured-state cases, %d worker calls identical, every one of %d conditional '
          'branches both ways, %d fault-stop cuts, %d missing-worker checks (%.1fs)' % (
              len(seeds), ', '.join('%s %d' % kv for kv in sorted(entries.items())), route_run,
              calls + route_calls, len(sites), faults, refusals, time.time() - started))
    if not states:
        print('NOTE: no route captures under %s; captured-state cases skipped' % ROUTE)


if __name__ == '__main__':
    main()
