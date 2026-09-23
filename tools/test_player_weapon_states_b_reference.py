#!/usr/bin/env python3
"""Execute the original player states 0x20, 0x21, 0x22 and compare
em_player_weapon_states_b.c.

docs/PLAYER_WEAPON_STATES_B.md. The user's pinned ELF supplies every
instruction and table (and the startup-reference capture the seed record of
some cases); none are embedded here. Routines executed unmodified:

  00173000  state 0x20, the R2 aiming stance
  001735C0  state 0x21, the light three-hit melee combo
  00173E60  state 0x22, the heavy melee stab
  00173DD0  the stab's in-swing yaw steer (private to 00173E60)
  copy_qw4  (00102958) the 64-byte matrix copy 00173000 makes

Every other callee is hooked, scripted per case and recorded (never
simulated as a claim about the callee); the native module gets the same
script through its workers. The test asserts that the hooked set is exactly
the set of jal targets of these routines, so no callee runs unhooked, and
that the original stores only to the memory the test compares.

Arithmetic: the COP1/VU0 ops run on test_player_fall_reference.FallEE (the
shared interpreter with every COP1 op and VU0 macro op routed through
tools/ee_float_model.py); no shared file is edited.

Default run (~10 s): a fixed-seed sample of the unit sweep, every
conditional branch of the four routines asserted both ways (except the four
"+302 byte == -1" tests, which can never be taken, asserted never taken),
the fault cuts, the missing-worker refusals and the table-bound faults.
EM_TEST_FULL=1 runs the whole sweep. No route beat reaches these states
(the census of +5 in the 15 route traces never shows 0x20..0x22), so there
is no world mode.
"""
import ctypes as C
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
from test_player_slide_reference import EE, read_elf, s32  # noqa: E402
from test_player_fall_reference import FallEE  # noqa: E402

MASK = 0xFFFFFFFF
LANE = os.environ.get('EM_LANE', 'player_weapon_states_b_reference')
OUT = ROOT / 'build' / LANE
CAPTURE = shared.DECOMP / 'build/startup-reference/playable_ee.bin'
PLAYER = 0x8102B0


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


# ======================================================================
# The routines and their callees
# ======================================================================

STANCE, COMBO, STAB, STEER, COPY = 0x173000, 0x1735C0, 0x173E60, 0x173DD0, 0x102958
SIZES = {STANCE: 0x5B8, COMBO: 0x810, STAB: 0x368, STEER: 0x8C, COPY: 0x24}
TRANSLATED = set(SIZES)
# The "+302 byte == -1" tests: a zero-extended byte never equals -1.
NEVER_TAKEN = {0x1737BC, 0x1739EC, 0x173BF8, 0x173FE8}

CALLEES = {
    0x1749A0: 'request', 0x1FBD50: 'sound', 0x11A070: 'stop', 0x174AC0: 'heading',
    0x1B12B0: 'approach', 0x1B1470: 'wrap', 0x11E620: 'atan2', 0x178B90: 'translate',
    0x1764E0: 'probes', 0x175900: 'floor', 0x1796C0: 'fall_check', 0x17C440: 'reentry',
    0x17C540: 'handoff', 0x1C6DA0: 'skeleton', 0x17A130: 'matrix', 0x17B300: 'reload',
    0x16F530: 'draw', 0x16F600: 'reload_wait', 0x17ABA0: 'pose', 0x199220: 'acquire',
    0x170A60: 'f0', 0x171320: 'f1', 0x171670: 'f2', 0x171B00: 'f3', 0x171E90: 'f4',
    0x1723D0: 'f5',
}
STANCE_CALLEES = {'reload', 'draw', 'reload_wait', 'pose', 'acquire', 'skeleton', 'matrix',
                  'f0', 'f1', 'f2', 'f3', 'f4', 'f5'}


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
    missing = sorted(t for t in targets if t not in CALLEES and t not in TRANSLATED)
    assert not missing, ('callees neither hooked nor translated', [hex(t) for t in missing])
    unused = sorted(t for t in CALLEES if t not in targets)
    assert not unused, ('hooked addresses no translated routine calls', [hex(t) for t in unused])
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


class CoverEE(FallEE):
    def __init__(self, elf):
        super().__init__(elf)
        self.outcomes = set()

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and in_translated(pc):
            self.outcomes.add((pc, b[0]))
        return b


# ======================================================================
# Native side (ctypes)
# ======================================================================

class LiveActor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', C.c_void_p), ('link_prev', C.c_void_p),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


class Scene(C.Structure):
    _fields_ = [('d8106E0', C.POINTER(C.c_uint32)), ('d810CA4', C.POINTER(C.c_uint8)),
                ('spad3A20', C.POINTER(C.c_uint32)), ('pad_pressed', C.POINTER(C.c_uint16)),
                ('spad3B78', C.POINTER(C.c_uint16))]


P = C.POINTER
LA = P(LiveActor)
I, U32, VP = C.c_int, C.c_uint32, C.c_void_p
FN = {
    'request': C.CFUNCTYPE(I, VP, LA, I, I, U32),
    'sound': C.CFUNCTYPE(I, VP, LA, I, P(I)),
    'stop': C.CFUNCTYPE(I, VP, I),
    'heading': C.CFUNCTYPE(I, VP, LA, I, P(I)),
    'approach': C.CFUNCTYPE(I, VP, U32, U32, U32, P(U32)),
    'wrap': C.CFUNCTYPE(I, VP, U32, P(U32)),
    'atan2': C.CFUNCTYPE(I, VP, U32, U32, P(U32)),
    'actor_int': C.CFUNCTYPE(I, VP, LA, I),
    'actor': C.CFUNCTYPE(I, VP, LA),
    'floor': C.CFUNCTYPE(I, VP, LA, I, P(I)),
    'link18': C.CFUNCTYPE(I, VP, U32, P(P(C.c_uint8))),
    'link20': C.CFUNCTYPE(I, VP, U32, P(U32), P(U32)),
    'bone': C.CFUNCTYPE(I, VP, C.c_uint, P(U32)),
    'clip_id': C.CFUNCTYPE(I, VP, U32, C.c_uint, P(C.c_int16)),
}
# EmPlayerWeaponBWorkers after context/scene, in header order: (field, kind, log name).
WORKER_FIELDS = (
    ('request', 'request', 'request'), ('sound', 'sound', 'sound'), ('stop_sound', 'stop', 'stop'),
    ('heading', 'heading', 'heading'), ('approach', 'approach', 'approach'),
    ('wrap', 'wrap', 'wrap'), ('atan2', 'atan2', 'atan2'),
    ('translate', 'actor_int', 'translate'), ('probes', 'actor', 'probes'),
    ('floor', 'floor', 'floor'), ('fall_check', 'actor', 'fall_check'),
    ('reentry', 'actor_int', 'reentry'), ('handoff', 'actor', 'handoff'),
    ('link18', 'link18', None), ('link20', 'link20', None), ('bone', 'bone', None),
    ('clip_id', 'clip_id', None),
    ('skeleton', 'actor', 'skeleton'), ('matrix', 'actor', 'matrix'),
    ('reload', 'actor_int', 'reload'), ('draw', 'actor_int', 'draw'),
    ('reload_wait', 'actor', 'reload_wait'), ('pose', 'actor', 'pose'),
    ('acquire', 'actor', 'acquire'), ('fire_00170A60', 'actor_int', 'f0'),
    ('fire_00171320', 'actor', 'f1'), ('fire_00171670', 'actor', 'f2'),
    ('fire_00171B00', 'actor', 'f3'), ('fire_00171E90', 'actor', 'f4'),
    ('fire_001723D0', 'actor', 'f5'),
)
SCENE_FIELDS = ('d8106E0', 'd810CA4', 'spad3A20', 'pad_pressed', 'spad3B78')
# What each state callback checks (em_player_weapon_b_bound_state*).
MELEE = ('request', 'sound', 'stop_sound', 'heading', 'translate', 'probes', 'floor',
         'fall_check', 'reentry', 'handoff', 'link18')
NEEDS = {
    'state20': ('request', 'sound', 'approach', 'wrap', 'atan2', 'link20', 'bone', 'clip_id',
                'skeleton', 'matrix', 'reload', 'draw', 'reload_wait', 'pose', 'acquire',
                'fire_00170A60', 'fire_00171320', 'fire_00171670', 'fire_00171B00',
                'fire_00171E90', 'fire_001723D0', 'scene', 'd8106E0', 'd810CA4', 'spad3A20'),
    'state21': MELEE + ('scene', 'pad_pressed', 'spad3B78'),
    'state22': MELEE + ('approach',),
}


class Workers(C.Structure):
    _fields_ = [('context', VP), ('scene', P(Scene))] + [(f, FN[k]) for f, k, _ in WORKER_FIELDS]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('weapon_b.dylib' if sys.platform == 'darwin' else 'weapon_b.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'src/game/em_player_weapon_states_b.c', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    W = P(Workers)
    for name in ('em_player_weapon_b_state20', 'em_player_weapon_b_state21',
                 'em_player_weapon_b_state22', 'em_player_weapon_b_00173DD0'):
        getattr(native, name).argtypes = [W, LA]
    return native


# ======================================================================
# Memory layout and scripted callee effects (identical on both sides)
# ======================================================================

ACTOR = 0x680000
TARGET, TARGET2 = 0x690000, 0x690100          # what +18 addresses (a hook may retarget it)
LINK20 = 0x6A0000
NODE_TABLE, NODE4 = 0x6C0000, 0x6D0000
CALLER_S1 = 0x5A5A0004                         # the $s1 0015B130 hands down (bit 2 set)
G_6E0, G_CA4, G_E74, S_3B78, S_3A20 = 0x8106E0, 0x810CA4, 0x810E74, 0x70003B78, 0x70003A20
REGION = {'actor': ACTOR, 'target': TARGET, 'target2': TARGET2, 'node': NODE4,
          'ca4': G_CA4, 'e74': G_E74, '3b78': S_3B78}
FLAGS = (0, 0x1000, 0x8000, 0x9000, 0x200, 0x1200, 0x8200)
GATES = (15.0, 16.0, 19.0, 20.0, 24.0, 25.0, 26.0, 28.0, 29.0, 30.0, 34.0, 41.0, 43.0)


def gate_value(rng):
    value = F(rng.choice(GATES))
    return rng.choice((value, value, value + 1, value - 1, F(0.0), F(100.0), F(-3.0)))


def effect_for(rng, name):
    """A hooked callee's scripted effect: its int return, a float return,
    and the stores it makes ((region, offset, size, value))."""
    e = {'ret': 0, 'fret': None, 'writes': []}
    w = e['writes']
    chance = rng.random
    if name == 'floor':
        e['ret'] = rng.choice((0, 1, 0x81))
        w.append(('actor', 0xA, 1, e['ret']))
    elif name == 'heading':
        e['ret'] = rng.choice((0, 1, 1, 7))
        if chance() < 0.5: w.append(('actor', 0x23F, 1, rng.randrange(4)))
        if chance() < 0.3: w.append(('actor', 0xC4, 4, F(rng.uniform(-3.2, 3.2))))
    elif name in ('approach', 'wrap', 'atan2'):
        e['fret'] = F(rng.choice((rng.uniform(-3.2, 3.2), 0.0, 3.14159274)))
    elif name == 'sound':
        e['ret'] = rng.choice((7, 0x1FF, -1, 0x12345, 0))
        if chance() < 0.3: w.append((rng.choice(('e74', '3b78')), 0, 2, rng.getrandbits(16)))
    elif name in ('request', 'translate', 'probes', 'fall_check', 'reentry', 'handoff'):
        if chance() < 0.3: w.append(('actor', 0x200, 4, rng.choice(FLAGS)))
        if chance() < 0.2: w.append(('actor', 0x3C, 4, gate_value(rng)))
        if chance() < 0.2: w.append(('actor', 0xB4, 4, F(rng.uniform(-300, 300))))
        if chance() < 0.2: w.append(('actor', 0x23F, 1, rng.randrange(4)))
        if chance() < 0.1: w.append(('actor', 0x18, 4, rng.choice((TARGET, TARGET2))))
        if chance() < 0.1: w.append(('actor', 0x2E, 2, rng.choice((0, 1))))
    elif name in STANCE_CALLEES:
        if chance() < 0.25: w.append(('actor', 6, 1, rng.choice((0, 1, 2, 3, 0x63, 0x65))))
        if chance() < 0.25: w.append(('actor', 0x1F0, 1, rng.choice((0x32, 0x33, 0x35, 0x31))))
        if chance() < 0.25: w.append(('actor', 0x275, 1, rng.choice((0, 1, 2, 3, 4, 5, 9, 13))))
        if chance() < 0.2: w.append(('actor', 0x2F2, 1, rng.choice((0, 1))))
        if chance() < 0.2: w.append(('actor', 0x274, 1, rng.choice((0, 1))))
        if chance() < 0.2: w.append(('actor', 0x2F0, 1, rng.choice((0, 2, 0xFF))))
        if chance() < 0.2: w.append(('actor', 0x317, 1, rng.choice((0, 1))))
        if chance() < 0.2: w.append(('actor', 5, 1, rng.choice((0x1D, 0x1E, 0x20))))
        if chance() < 0.2: w.append(('ca4', 0, 1, rng.choice((0, 1, 2))))
        if name in ('skeleton', 'matrix'):
            for _ in range(rng.randrange(4)):
                w.append(('node', 0x90 + 4 * rng.randrange(16), 4, F(rng.uniform(-500, 500))))
    return e


class Script:
    def __init__(self, seed, fail_at=None):
        self.seed, self.count, self.fail_at = seed, 0, fail_at

    def next(self, name):
        rng = random.Random('%d:%d:%s' % (self.seed, self.count, name))
        index = self.count
        self.count += 1
        return index, effect_for(rng, name)


def put(buf, offset, size, value):
    buf[offset:offset + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')


CAPTURED = None
STATES = {'state20': (0, 0, 1, 1, 2, 2, 2, 3, 0x63, 0x63, 0x64, 0x64, 0x65, 0x66, 0x66, 4, 0x62),
          'state21': (0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 0x50, 0x51, 0x52, 0x63, 0x64, 4, 0x4F),
          'state22': (0, 1, 2, 2, 3, 3, 4, 0x50, 0x51, 0x51, 0x52, 0x63, 0x64, 5),
          'steer': (2,)}


def make_case(seed):
    rng = random.Random(seed)
    entry = rng.choice(('state20',) * 4 + ('state21',) * 5 + ('state22',) * 4 + ('steer',))
    if CAPTURED is not None and rng.random() < 0.3:
        actor = bytearray(CAPTURED)
    else:
        actor = bytearray(rng.getrandbits(8) for _ in range(0x320))
    put(actor, 4, 1, 1)
    put(actor, 5, 1, rng.choice((0x1D, 0x1E, 0x1F, 0x20, 0x20, 0x21, 0x22)))
    put(actor, 6, 1, rng.choice(STATES[entry]))
    put(actor, 7, 1, rng.choice((0, 1, 2, 2, 3, 3, 4, 5)))
    put(actor, 0x18, 4, TARGET)
    put(actor, 0x20, 4, LINK20)
    put(actor, 0x236, 1, rng.choice((0, 0, 1)))
    put(actor, 0x23F, 1, rng.randrange(4))
    put(actor, 0x275, 1, rng.choice((0, 1, 2, 3, 4, 5) * 3 + (6, 7, 8, 9, 13, 19, 40, 0xFF)))
    put(actor, 0x1F0, 1, rng.choice((0x32, 0x33, 0x35, 0x31, 0x34)))
    for off in (0x2F2, 0x274, 0x317):
        put(actor, off, 1, rng.choice((0, 1)))
    put(actor, 0x2F0, 1, rng.choice((0, 1, 2, 3, 0xFF)))
    put(actor, 0x200, 4, rng.choice(FLAGS) | (rng.getrandbits(8) if rng.random() < 0.3 else 0))
    put(actor, 0x3C, 4, gate_value(rng))
    put(actor, 0x28, 2, rng.choice((0, 0, 1, 4, 8, 0xFFFF, 0x8000)))
    put(actor, 0x2E, 2, rng.choice((0, 0, 0, 1, 0x100)))
    put(actor, 0x302, 1, rng.choice((0xFF, 0, 7, rng.randrange(256))))
    if rng.random() < 0.9:
        for off in (0x278, 0x27C, 0x26C, 0x270):
            put(actor, off, 4, F(rng.uniform(-0.2, 1.2)))
        for off in (0x218, 0xC4):
            put(actor, off, 4, F(rng.uniform(-3.2, 3.2)))
        put(actor, 0xB4, 4, F(rng.uniform(-300, 300)))
        put(actor, 0x1FC, 4, rng.choice((F(1.0), F(0.5), rng.getrandbits(32))))
    target = bytearray(rng.getrandbits(8) for _ in range(0x40))
    target[0xA] = rng.choice((0, 0, 1, 0x80))
    target2 = bytearray(rng.getrandbits(8) for _ in range(0x40))
    target2[0xA] = rng.choice((0, 1))
    node = bytearray(0xD0)
    for i in range(0x90, 0xD0, 4):
        put(node, i, 4, F(rng.uniform(-500, 500)))
    return {
        'seed': seed, 'entry': entry, 'actor': bytes(actor), 'target': bytes(target),
        'target2': bytes(target2), 'node': bytes(node),
        'link20': (F(rng.uniform(-500, 500)), F(rng.uniform(-500, 500))),
        'ca4': rng.choice((0, 0, 1, 1, 2, 0xFF)), 'e74': rng.choice((0, 0, 0x20, 0xFFFF, rng.getrandbits(16))),
        '3b78': rng.choice((0x20, 0x20, 0, rng.getrandbits(16))),
        '6e0': rng.getrandbits(32), '3a20': rng.getrandbits(32),
    }


# ======================================================================
# The original side
# ======================================================================

ALLOWED = ((ACTOR, 0x320), (TARGET, 0x40), (TARGET2, 0x40), (NODE4, 0xD0), (G_6E0, 4),
           (G_CA4, 1), (G_E74, 2), (S_3B78, 2), (S_3A20, 4))


class UnitOracle:
    def __init__(self, elf):
        self.ee = CoverEE(elf)
        for address, name in CALLEES.items():
            self.ee.hooks[address] = self.hook(name)
        self.stray = set()
        save = self.ee.save

        def guarded(address, value, size=4):
            address &= MASK
            if not (0x7F000000 <= address < 0x7F100000) and not any(
                    base <= address and address + size <= base + n for base, n in ALLOWED):
                self.stray.add(address)
            save(address, value, size)
        self.ee.save = guarded

    def hook(self, name):
        def run(ee):
            self.log.append(self.log_entry(name, ee))
            _, e = self.script.next(name)
            for region, offset, size, value in e['writes']:
                ee.save(REGION[region] + offset, value, size)
            if e['fret'] is not None:
                ee.f[0] = e['fret']
            ee.ret_int(e['ret'])
        return run

    def log_entry(self, name, ee):
        a = ee.arg
        if name not in ('stop', 'approach', 'wrap', 'atan2'):
            assert a(0) == ACTOR, (name, hex(a(0)))
        s1 = ee.r[17] & MASK
        if name in STANCE_CALLEES:
            assert s1 == ACTOR, (name, '$s1 is not the record', hex(s1))
        if name == 'probes':
            assert s1 == CALLER_S1, ('001764E0 $s1', hex(s1))
        if name == 'request': return (name, s32(a(1)), s32(a(2)), ee.f[12] & MASK)
        if name == 'sound':
            assert s32(a(2)) == 0 and ee.f[12] & MASK == F(300.0), ('sound arguments', a(2), ee.f[12])
            return (name, s32(a(1)))
        if name == 'stop': return (name, s32(a(0)))
        if name in ('heading', 'translate', 'floor', 'reentry', 'reload', 'draw', 'f0'):
            return (name, s32(a(1)))
        if name == 'approach': return (name, ee.f[12] & MASK, ee.f[13] & MASK, ee.f[14] & MASK)
        if name == 'wrap': return (name, ee.f[12] & MASK)
        if name == 'atan2': return (name, ee.f[12] & MASK, ee.f[13] & MASK)
        return (name,)

    def run(self, case, script):
        ee = self.ee
        self.script, self.log = script, []
        ee.r, ee.rh = [0] * 32, [0] * 32
        ee.f, ee.acc, ee.cond = [0] * 32, 0, False
        ee.vacc, ee.q = [0, 0, 0, 0], 0
        ee.r[28], ee.r[29], ee.r[17] = 0x27D370, shared.STACK_TOP, CALLER_S1
        ee.write(ACTOR, case['actor'])
        ee.write(TARGET, case['target']); ee.write(TARGET2, case['target2'])
        ee.write(NODE4, case['node'])
        ee.save(0x275B40, NODE_TABLE); ee.save(NODE_TABLE + 0x10, NODE4)
        ee.save(LINK20 + 0xC0, case['link20'][0]); ee.save(LINK20 + 0xC8, case['link20'][1])
        ee.save(G_6E0, case['6e0']); ee.save(G_CA4, case['ca4'], 1)
        ee.save(G_E74, case['e74'], 2); ee.save(S_3B78, case['3b78'], 2)
        ee.save(S_3A20, case['3a20'])
        self.stray = set()
        entry = {'state20': STANCE, 'state21': COMBO, 'state22': STAB, 'steer': STEER}[case['entry']]
        ee.call(entry, (ACTOR,))
        assert not self.stray, ('the original stored outside the compared memory',
                                sorted(hex(x) for x in self.stray))
        return {'actor': ee.read(ACTOR, 0x320), 'target': ee.read(TARGET, 0x40),
                'target2': ee.read(TARGET2, 0x40), 'node': ee.read(NODE4, 0xD0),
                'globals': (ee.load(G_6E0), ee.load(G_CA4, 1), ee.load(G_E74, 2),
                            ee.load(S_3B78, 2), ee.load(S_3A20)),
                'log': self.log}


# ======================================================================
# The native side
# ======================================================================

class NativeRun:
    def __init__(self, native, case, script, missing=None):
        self.native, self.case, self.script, self.log = native, case, script, []
        self.live = LiveActor()
        C.memmove(self.live.bytes, case['actor'], 0x320)
        self.target = (C.c_uint8 * 0x40).from_buffer_copy(case['target'])
        self.target2 = (C.c_uint8 * 0x40).from_buffer_copy(case['target2'])
        self.node = bytearray(case['node'])
        self.g6E0, self.gCA4 = C.c_uint32(case['6e0']), C.c_uint8(case['ca4'])
        self.gE74, self.s3B78 = C.c_uint16(case['e74']), C.c_uint16(case['3b78'])
        self.s3A20 = C.c_uint32(case['3a20'])
        self.scene = Scene(*(C.pointer(v) if name != missing else None for name, v in zip(
            SCENE_FIELDS, (self.g6E0, self.gCA4, self.s3A20, self.gE74, self.s3B78))))
        self.keep = []
        fields = {}
        for field, kind, name in WORKER_FIELDS:
            fields[field] = FN[kind](self.worker(field, name)) if field != missing else FN[kind]()
            self.keep.append(fields[field])
        self.workers = Workers(None, C.pointer(self.scene) if missing != 'scene' else None, **fields)

    def store(self, region, offset, size, value):
        data = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')
        if region == 'actor':
            for i in range(size): self.live.bytes[offset + i] = data[i]
        elif region in ('target', 'target2'):
            buf = self.target if region == 'target' else self.target2
            for i in range(size): buf[offset + i] = data[i]
        elif region == 'node':
            self.node[offset:offset + size] = data
        elif region == 'ca4': self.gCA4.value = value & 0xFF
        elif region == 'e74': self.gE74.value = value & 0xFFFF
        elif region == '3b78': self.s3B78.value = value & 0xFFFF

    def call(self, name, entry):
        self.log.append(entry)
        index, e = self.script.next(name)
        if self.script.fail_at == index:
            return None
        for write in e['writes']:
            self.store(*write)
        return e

    def worker(self, field, name):
        def plain(_, *args):
            extra = tuple(args[1:]) if field in ('translate', 'reentry', 'reload', 'draw',
                                                 'fire_00170A60') else ()
            return -1 if self.call(name, (name,) + extra) is None else 0

        if field == 'request':
            return lambda _, a, clip, force, blend: (
                -1 if self.call(name, (name, clip, force, blend)) is None else 0)
        if field == 'sound':
            def sound(_, a, id, out):
                e = self.call(name, (name, id))
                if e is None: return -1
                out[0] = s32(e['ret'])
                return 0
            return sound
        if field == 'stop_sound':
            return lambda _, handle: -1 if self.call(name, (name, handle)) is None else 0
        if field in ('heading', 'floor'):
            def with_result(_, a, arg, out):
                e = self.call(name, (name, arg))
                if e is None: return -1
                out[0] = s32(e['ret'])
                return 0
            return with_result
        if field in ('approach', 'wrap', 'atan2'):
            def fret(_, *args):
                e = self.call(name, (name,) + tuple(args[:-1]))
                if e is None: return -1
                args[-1][0] = e['fret']
                return 0
            return fret
        if field == 'link18':
            def link18(_, word, out):
                buf = {TARGET: self.target, TARGET2: self.target2}[word]
                out[0] = C.cast(buf, P(C.c_uint8))
                return 0
            return link18
        if field == 'link20':
            def link20(_, word, c0, c8):
                assert word == LINK20, hex(word)
                c0[0], c8[0] = self.case['link20']
                return 0
            return link20
        if field == 'bone':
            def bone(_, slot, out):
                assert slot == 4, slot
                for i in range(16):
                    out[i] = int.from_bytes(self.node[0x90 + 4 * i:0x94 + 4 * i], 'little')
                return 0
            return bone
        if field == 'clip_id':
            def clip_id(_, table, index, out):
                assert table in (0x248B88, 0x248C68), hex(table)
                out[0] = struct.unpack_from('<h', ELF, table - 0x100000 + 0x300 + 2 * index)[0]
                return 0
            return clip_id
        return plain

    def run(self):
        n, entry = self.native, self.case['entry']
        W, A = C.byref(self.workers), C.byref(self.live)
        fn = {'state20': n.em_player_weapon_b_state20, 'state21': n.em_player_weapon_b_state21,
              'state22': n.em_player_weapon_b_state22, 'steer': n.em_player_weapon_b_00173DD0}[entry]
        result = fn(W, A)
        return result, {'actor': bytes(self.live.bytes), 'target': bytes(self.target),
                        'target2': bytes(self.target2), 'node': bytes(self.node),
                        'globals': (self.g6E0.value, self.gCA4.value, self.gE74.value,
                                    self.s3B78.value, self.s3A20.value),
                        'log': self.log}


# ======================================================================
# Cases
# ======================================================================

ELF = NATIVE = ORACLE = None


def compare(where, want, got):
    assert want['log'] == got['log'], (where, 'worker calls', want['log'], got['log'])
    if want['actor'] != got['actor']:
        diff = [hex(k) for k in range(0x320) if want['actor'][k] != got['actor'][k]]
        raise AssertionError((where, 'actor bytes differ at', diff[:24]))
    for key in ('target', 'target2', 'node', 'globals'):
        assert want[key] == got[key], (where, key, want[key], got[key])


def run_case(seed):
    global ORACLE
    if ORACLE is None:
        ORACLE = UnitOracle(ELF)
    case = make_case(seed)
    want = ORACLE.run(case, Script(seed))
    result, got = NativeRun(NATIVE, case, Script(seed)).run()
    where = (seed, case['entry'], case['actor'][6], case['actor'][7])
    assert result == 0, (where, 'native fault', result)
    compare(where, want, got)
    faults = 0
    if want['log'] and seed % 5 == 0:
        k = random.Random(seed).randrange(len(want['log']))
        result, cut = NativeRun(NATIVE, case, Script(seed, fail_at=k)).run()
        assert result == -1, (where, 'fault not reported', k)
        assert cut['log'] == want['log'][:k + 1], (where, 'calls after a fault', k)
        faults = 1
    return case['entry'], len(want['log']), faults, tuple(sorted(ORACLE.ee.outcomes))


def refusal_checks(native):
    """Each binding a state callback needs, removed: -1 before any write or
    call. Each binding it does not need, removed: it still runs."""
    count = 0
    names = [f for f, _, _ in WORKER_FIELDS] + list(SCENE_FIELDS) + ['scene']
    for entry, needs in NEEDS.items():
        base = next(make_case(s) for s in range(9000, 99999) if make_case(s)['entry'] == entry)
        for name in names:
            run = NativeRun(native, base, Script(base['seed']), missing=name)
            result, got = run.run()
            if name in needs:
                assert result == -1 and got['log'] == [] and got['actor'] == base['actor'] and \
                    got['target'] == base['target'] and got['globals'][0] == base['6e0'], (entry, name)
            else:
                assert result == 0, (entry, name, 'an unneeded binding blocked the routine')
            count += 1
    return count


def table_fault_checks(native):
    """The table bounds the translation adds (+236 above 1 at a melee table
    read, +23F above 3 at 00173DD0's read) fault instead of reading past the
    tables."""
    checks = 0
    rng = random.Random(77)
    for _ in range(40):
        case = make_case(rng.randrange(1 << 30))
        actor = bytearray(case['actor'])
        kind = rng.choice(('row', 'gait'))
        if kind == 'row':
            entry = rng.choice(('state21', 'state22'))
            actor[6], actor[7], actor[0x236] = (1, 0, 2) if entry == 'state21' else (0, 0, 2)
        else:
            entry = 'steer'
            actor[0x23F] = 4
        case = dict(case, entry=entry, actor=bytes(actor))

        class Always(Script):   # heading returns nonzero and writes nothing
            def next(self, name):
                index, e = Script.next(self, name)
                if name == 'heading': e = dict(e, ret=1, writes=[])
                return index, e
        result, _ = NativeRun(native, case, Always(case['seed'])).run()
        assert result == -1, (kind, entry, 'table bound not enforced')
        checks += 1
    return checks


def main():
    global ELF, NATIVE, CAPTURED
    started = time.time()
    ELF = read_elf()
    if CAPTURE.exists():
        CAPTURED = CAPTURE.read_bytes()[PLAYER:PLAYER + 0x320]
    callees = check_callee_set(ELF)
    NATIVE = build_native()
    total = reference_mode.pick(16000, 16000)
    seeds = reference_mode.select(range(total), 3000, 0x5EED)
    results = reference_mode.parallel_map(run_case, seeds)
    outcomes, entries, faults, calls = set(), {}, 0, 0
    for entry, count, fault, cover in results:
        outcomes.update(cover)
        entries[entry] = entries.get(entry, 0) + 1
        faults += fault
        calls += count
    sites = branch_sites(ELF)
    assert NEVER_TAKEN <= sites, 'the -1 test sites moved'
    taken_never = sorted('%06X' % pc for pc in NEVER_TAKEN if (pc, True) in outcomes)
    assert not taken_never, ('a "+302 == -1" test was taken', taken_never)
    missing = sorted('%06X %s' % (pc, 'taken' if taken else 'not taken')
                     for pc in sites for taken in (True, False)
                     if (pc, taken) not in outcomes and not (taken and pc in NEVER_TAKEN))
    assert not missing, ('branch outcomes never exercised', missing)
    absent = [e for e in STATES if e not in entries]
    assert not absent, ('entry points never run', absent)
    refusals = refusal_checks(NATIVE)
    bounds = table_fault_checks(NATIVE)
    reference_mode.banner(reference_mode.part(len(seeds), total, 'cases'),
                          '%d jal targets (all hooked or translated)' % callees)
    print('player weapon states 0x20-0x22 vs original instructions: PASS %d cases (%s), %d worker '
          'calls identical, %d conditional branches both ways (4 never-taken -1 tests asserted '
          'never taken), %d fault-stop cuts, %d binding checks, %d table-bound faults (%.1fs)' % (
              len(seeds), ', '.join('%s %d' % kv for kv in sorted(entries.items())), calls,
              len(sites), faults, refusals, bounds, time.time() - started))


if __name__ == '__main__':
    main()
