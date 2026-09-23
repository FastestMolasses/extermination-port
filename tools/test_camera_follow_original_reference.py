#!/usr/bin/env python3
"""Execute the original follow-camera routines and compare em_camera_follow_original.c.

docs/CAMERA_FOLLOW_ORIGINAL.md. The user's pinned ELF (and, for the captured
modes, the captured RAM) supplies every instruction and table; none are
embedded here. Routines executed unmodified:

  001921D0  the follow update        0018D7B0  the solve dispatch
  0018D330  the prepass              00191390  the per-state heights
  0018C6A0 / 0018C4B0  the eye chases
  00191D40 / 00192010  the eye height chases
  00191120  the bounded yaw step
  0011DF78 and the SDK vector leaves 001028D0 / 001028B8 / 00102760 /
  00103230 / 00102738 / 001026A0 / 00102948 / 001031E0

Every other callee is hooked, scripted per case and recorded (never
simulated as a claim about the callee); the native module gets the same
script through its workers. The test asserts that the hooked set is exactly
the set of jal targets of these routines, so no callee runs unhooked.

Arithmetic: CameraEE is the fall test's FallEE (every COP1 op and VU0 macro
op through tools/ee_float_model.py); the shared files are not edited.

Default run (~10 s): unit cases with every conditional branch of the
translated routines taken both ways, fail-stop cuts, missing-worker
refusals, and the captured check: the original camera frame 0018B9C0 run
once over each captured RAM image (startup-reference state 04 and every
route beat's source) with these translations in place of the originals
(workers bound to the original callees in the same EE), whole RAM and
scratchpad compared with the all-original run. EM_TEST_FULL=1: the exhaustive
sweep. EM_TEST_WORLD=1: the route beats replayed (the player stage as
test_player_slide_reference.RouteReplay drives it, then the original camera
frame 0018B9C0 every frame), original against translated, whole RAM and
scratchpad every frame.
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
from test_player_slide_reference import EE, read_elf, s32, number  # noqa: E402
from test_player_fall_reference import FallEE, nested_bits  # noqa: E402

MASK = 0xFFFFFFFF
LANE = os.environ.get('EM_LANE', 'camera_follow_original_reference')
OUT = ROOT / 'build' / LANE


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


# ======================================================================
# The routines and their callees
# ======================================================================

FOLLOW, SOLVE, PREPASS, HEIGHTS = 0x1921D0, 0x18D7B0, 0x18D330, 0x191390
CHASE_XZ, CHASE_Y, HEIGHT, HEIGHT2, YAW = 0x18C6A0, 0x18C4B0, 0x191D40, 0x192010, 0x191120
SIZES = {FOLLOW: 0x1490, SOLVE: 0x154, PREPASS: 0x478, HEIGHTS: 0x108, CHASE_XZ: 0x1A8,
         CHASE_Y: 0xE8, HEIGHT: 0x2C4, HEIGHT2: 0x1B8, YAW: 0xEC,
         0x1028D0: 0x14, 0x1028B8: 0x14, 0x102760: 0x34, 0x103230: 0x18, 0x102738: 0x24,
         0x1026A0: 0x2C, 0x102948: 0xC, 0x1031E0: 0x1C, 0x11DF78: 0x1C}
TRANSLATED = set(SIZES)
CAMERA_FRAME = 0x18B9C0


def in_translated(pc):
    return any(start <= pc < start + size for start, size in SIZES.items())


# Original callee -> worker name (every jal target of the routines above).
CALLEES = {
    0x1B12B0: 'approach', 0x1B1470: 'wrap', 0x1B1240: 'heading', 0x11E2A8: 'sine',
    0x11DE90: 'cosine', 0x230000: 'tether', 0x18DD20: 'solve', 0x18F870: 'solve_aim',
    0x18D910: 'bounds', 0x19A910: 'segment', 0x19B7D0: 'ground', 0x1029C0: 'identity',
    0x102C58: 'euler',
}


def jal_targets(elf):
    ee = EE(elf)
    targets = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            if word >> 26 == 3:
                targets.add((word & 0x3FFFFFF) << 2)
            assert not (word >> 26 == 0 and word & 63 == 9), ('jalr inside a translated routine', hex(pc))
    return targets


def check_callee_set(elf):
    targets = jal_targets(elf)
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


# ======================================================================
# Memory layout (original addresses)
# ======================================================================

CAM, PLAYER, OTHER = 0x8101E0, 0x8102B0, 0x690000
HIT_POINTER, HIT_Y, HIT_RECORD = 0x700031D0, 0x700031B4, 0x6E0000
GLOBAL_WORDS = (('eye', 0x8105D0, 4), ('target', 0x8105E0, 4), ('d690', 0x810690, 1),
                ('d698', 0x810698, 1), ('d69C', 0x81069C, 1))
GLOBAL_BYTES = (('area', 0x810700), ('d701', 0x810701), ('d702', 0x810702))
SCRATCH = (('s38A0', 0x700038A0, 4), ('s38B0', 0x700038B0, 4), ('s38C0', 0x700038C0, 4),
           ('s3910', 0x70003910, 4), ('s3A20', 0x70003A20, 4), ('s3B50', 0x70003B50, 4),
           ('s3400', 0x70003400, 16), ('s3600', 0x70003600, 4))


def compared_addresses():
    out = set(range(CAM, CAM + 0xD0)) | set(range(PLAYER, PLAYER + 0x320)) | set(range(OTHER, OTHER + 0x320))
    for _, address, count in GLOBAL_WORDS + SCRATCH:
        out |= set(range(address, address + 4 * count))
    for _, address in GLOBAL_BYTES:
        out.add(address)
    return out


COMPARED = compared_addresses()


# ======================================================================
# The interpreter
# ======================================================================

class CameraEE(FallEE):
    """FallEE (the measured float model) recording branch outcomes inside
    the translated routines and every store the instructions make."""

    def __init__(self, elf, ram=None, spad=None, cover=False):
        super().__init__(elf, ram, spad)
        self.outcomes = set()
        self.cover = cover
        self.writes = None          # a set while recording, else None
        self.in_hook = False

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if self.cover and b is not None and in_translated(pc):
            self.outcomes.add((pc, b[0]))
        return b

    def mmi(self, word, pc):
        """The shared core plus the MMI forms the camera frame's other
        callees execute (PEXTLW: rd = rs.w1 rt.w1 rs.w0 rt.w0, high to low;
        PEXTUW the same over the upper words)."""
        rs, rt, rd = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        fn, sub = word & 63, word >> 6 & 31
        if fn == 0x08 and sub == 0x12:
            if rd:
                a, b = self.r[rs] & 0xFFFFFFFFFFFFFFFF, self.r[rt] & 0xFFFFFFFFFFFFFFFF
                self.r[rd] = (b & MASK) | (a & MASK) << 32
                self.rh[rd] = (b >> 32) | (a >> 32) << 32
            return
        if fn == 0x28 and sub == 0x12:                  # PEXTUW: rs.w3 rt.w3 rs.w2 rt.w2
            if rd:
                a, b = self.rh[rs] & 0xFFFFFFFFFFFFFFFF, self.rh[rt] & 0xFFFFFFFFFFFFFFFF
                self.r[rd] = (b & MASK) | (a & MASK) << 32
                self.rh[rd] = (b >> 32) | (a >> 32) << 32
            return
        super().mmi(word, pc)

    def save(self, address, value, size=4):
        if self.writes is not None and not self.in_hook:
            address &= MASK
            if not 0x7F000000 <= address < 0x7F100000:
                self.writes.update(range(address, address + size))
        super().save(address, value, size)


# ======================================================================
# Native side (ctypes)
# ======================================================================

U32, I, VP = C.c_uint32, C.c_int, C.c_void_p
P = C.POINTER


class Record(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0xD0)]


class LiveActor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', VP), ('link_prev', VP),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


class Globals(C.Structure):
    _fields_ = [('eye', P(U32)), ('target', P(U32)), ('d690', P(U32)), ('d698', P(U32)),
                ('d69C', P(U32)), ('area', P(C.c_uint8)), ('d701', P(C.c_uint8)), ('d702', P(C.c_uint8))]


class Scratch(C.Structure):
    _fields_ = [(name, U32 * count) for name, _, count in SCRATCH]


class Hit(C.Structure):
    _fields_ = [('result', C.c_int32), ('record_1A', C.c_uint16), ('point_y', U32)]


REC, LA = P(Record), P(LiveActor)
FN = {
    'approach': C.CFUNCTYPE(I, VP, U32, U32, U32, P(U32)),
    'wrap': C.CFUNCTYPE(I, VP, U32, P(U32)),
    'heading': C.CFUNCTYPE(I, VP, P(U32), U32, U32, P(U32)),
    'sine': C.CFUNCTYPE(I, VP, U32, P(U32)),
    'cosine': C.CFUNCTYPE(I, VP, U32, P(U32)),
    'tether': C.CFUNCTYPE(I, VP, REC, LA),
    'solve': C.CFUNCTYPE(I, VP, REC, LA, I, I, P(I)),
    'solve_aim': C.CFUNCTYPE(I, VP, REC, LA, I, I, P(I)),
    'bounds': C.CFUNCTYPE(I, VP, REC, LA, I),
    'segment': C.CFUNCTYPE(I, VP, P(U32), P(U32), I, P(Hit)),
    'ground': C.CFUNCTYPE(I, VP, P(U32), P(U32), P(I)),
    'identity': C.CFUNCTYPE(I, VP, P(U32)),
    'euler': C.CFUNCTYPE(I, VP, P(U32), P(U32), P(U32)),
}
WORKER_FIELDS = ('approach', 'wrap', 'heading', 'sine', 'cosine', 'tether', 'solve', 'solve_aim',
                 'bounds', 'segment', 'ground', 'identity', 'euler')


class Workers(C.Structure):
    _fields_ = [('context', VP)] + [(name, FN[name]) for name in WORKER_FIELDS]


class World(C.Structure):
    _fields_ = [('cam', REC), ('player', LA), ('globals', P(Globals)), ('scratch', P(Scratch)),
                ('workers', P(Workers))]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('camera_follow.dylib' if sys.platform == 'darwin' else 'camera_follow.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'src/game/em_camera_follow_original.c', 'src/game/em_sdk_math_original.c',
                    '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    W = P(World)
    n.em_camera_follow_bound.argtypes = [W]
    n.em_camera_follow_001921D0.argtypes = [W, LA, I]
    n.em_camera_follow_0018D7B0.argtypes = [W, I, P(I)]
    n.em_camera_follow_0018D330.argtypes = [W, LA, I, I]
    n.em_camera_follow_00191390.argtypes = [REC, LA]
    n.em_camera_follow_0018C6A0.argtypes = [P(U32), P(U32), U32, P(I)]
    n.em_camera_follow_0018C4B0.argtypes = [P(U32), U32, U32, P(I)]
    n.em_camera_follow_00191D40.argtypes = [REC, P(Globals), U32, U32]
    n.em_camera_follow_00192010.argtypes = [REC, U32, U32, U32]
    n.em_camera_follow_00191120.argtypes = [P(Workers), U32, U32, U32, U32, P(U32)]
    return n


class NativeState:
    """The native records, the canonical global storage the Globals pointers
    address, and the scratch, addressed by original address (so scripted
    effects apply identically on both sides)."""

    def __init__(self):
        self.cam, self.player, self.other = Record(), LiveActor(), LiveActor()
        self.g = {name: (U32 * count)() for name, _, count in GLOBAL_WORDS}
        self.g.update({name: (C.c_uint8 * 1)() for name, _ in GLOBAL_BYTES})
        words = {name for name, _, _ in GLOBAL_WORDS}
        self.globals = Globals(**{name: C.cast(array, P(U32) if name in words else P(C.c_uint8))
                                  for name, array in self.g.items()})
        self.scratch = Scratch()

    def slot(self, address):
        """(ctypes array, byte offset, unit) holding one address: unit 1 for
        byte arrays, 4 for word arrays."""
        for base, obj, size in ((CAM, self.cam, 0xD0), (PLAYER, self.player, 0x320),
                                (OTHER, self.other, 0x320)):
            if base <= address < base + size:
                return obj.bytes, address - base, 1
        for name, base, count in GLOBAL_WORDS:
            if base <= address < base + 4 * count:
                return self.g[name], address - base, 4
        for name, base in GLOBAL_BYTES:
            if address == base:
                return self.g[name], 0, 1
        for name, base, count in SCRATCH:
            if base <= address < base + 4 * count:
                return getattr(self.scratch, name), address - base, 4
        raise AssertionError(('address the native state does not hold', hex(address)))

    def save(self, address, value, size=4):
        for i in range(size):
            self.save_byte(address + i, value >> (8 * i) & 0xFF)

    def save_byte(self, address, byte):
        field, at, unit = self.slot(address)
        if unit == 1:
            field[at] = byte
        else:
            shift = 8 * (at % 4)
            field[at // 4] = (field[at // 4] & ~(0xFF << shift) | byte << shift) & MASK

    def load_byte(self, address):
        field, at, unit = self.slot(address)
        return field[at] if unit == 1 else field[at // 4] >> (8 * (at % 4)) & 0xFF

    def load(self, address, size=4):
        return sum(self.load_byte(address + i) << (8 * i) for i in range(size))

    def image(self):
        return bytes(self.load_byte(a) for a in sorted(COMPARED))


def ee_image(ee):
    return bytes(ee.load(a, 1) for a in sorted(COMPARED))


def image_diff(a, b):
    order = sorted(COMPARED)
    return [hex(order[i]) for i in range(len(order)) if a[i] != b[i]]


# ======================================================================
# Scripted callee effects (identical on both sides)
# ======================================================================

ANGLES = (0.0, -0.0, 0.5, -0.5, 3.14159274, -3.14159274, 1.5707964, 0.0523599, -0.0523599)


def angle(rng):
    return F(rng.choice(ANGLES) if rng.random() < 0.3 else rng.uniform(-3.2, 3.2))


CAM_WORDS = (0x0C, 0x10, 0x14, 0x18, 0x20, 0x24, 0x28, 0x44, 0x54, 0x60, 0x90)


def effect_for(rng, name, entry):
    """What a hooked callee does: return value (v0), f0, the writes it
    makes (address, size, value) and, for segment, the hit data."""
    e = {'ret': 0, 'fret': None, 'writes': [], 'hit': None, 'out': None}
    w = e['writes']
    chance = rng.random
    if name == 'approach':
        target, current = entry[1], entry[2]
        pick = chance()
        e['fret'] = target if pick < 0.45 else current if pick < 0.6 else angle(rng)
    elif name == 'wrap':
        pick = chance()
        e['fret'] = entry[1] if pick < 0.5 else angle(rng) if pick < 0.85 else rng.choice((0, 0x80000000))
    elif name in ('heading', 'sine', 'cosine'):
        if name == 'heading':
            e['fret'] = angle(rng)
        else:
            e['fret'] = F(rng.choice((0.0, 1.0, -1.0, 0.70710677))) if chance() < 0.3 else F(rng.uniform(-1, 1))
        if chance() < 0.1:
            # a state change behind the call: the idle timer (00193448) must
            # re-read the player state word rather than reuse the dispatch's
            state = rng.choice((1, 2, 2, 3))
            w.extend(((PLAYER + 0x230, 4, state), (OTHER + 0x230, 4, state)))
    elif name in ('tether', 'solve', 'solve_aim', 'bounds'):
        for _ in range(rng.choice((0, 0, 1, 2, 4))):
            w.append((CAM + rng.choice(CAM_WORDS), 4, F(rng.uniform(-400, 400))))
        if chance() < 0.2:
            w.append((CAM + 0x5A, 2, rng.choice((0, 1, 0x80, 0x81, 0x18))))
        if chance() < 0.2:
            w.append((0x8105D0 + 4 * rng.randrange(3), 4, F(rng.uniform(-400, 400))))
        if name != 'tether' and chance() < 0.3:
            w.append((0x700038A0 + 4 * rng.randrange(4), 4, F(rng.uniform(-2, 2))))
        if name in ('solve', 'solve_aim'):
            e['ret'] = rng.choice((0, 0, 0, 1, 2, 4, 8, 9, 0x10, 0x1F, 0x40, 0x80, 0xC0, 0x141,
                                   rng.randrange(256)))
    elif name == 'segment':
        e['ret'] = rng.choice((0, 0, 1, 2, 4))
        e['hit'] = (rng.choice((0, 0x2000, 0x8800, 0x0800, 0x8000, 0xA000, 0x2800, 0x0400,
                                rng.randrange(0x10000))), F(rng.uniform(-100, 400)))
    elif name == 'ground':
        e['ret'] = rng.choice((0, 0, 1, 4, -1))
    elif name == 'identity':
        if chance() < 0.7:
            e['out'] = [F(1.0) if i % 5 == 0 else 0 for i in range(16)]
        else:
            e['out'] = [F(rng.uniform(-2, 2)) for _ in range(16)]
    elif name == 'euler':
        e['out'] = [F(rng.uniform(-1, 1)) for _ in range(12)] + [F(rng.uniform(-5, 5)) for _ in range(3)] + [F(1.0)]
    return e


class Script:
    def __init__(self, seed, fail_at=None):
        self.seed, self.count, self.fail_at = seed, 0, fail_at

    def next(self, name, entry):
        rng = random.Random('%d:%d:%s:%r' % (self.seed, self.count, name, entry))
        index = self.count
        self.count += 1
        return index, effect_for(rng, name, entry)


# ======================================================================
# Cases
# ======================================================================

STATES = (1, 1, 1, 2, 3, 4, 6, 7, 8, 9, 0xA, 0xF, 0x13, 0x14, 0x15, 0x18, 0x19, 0x26, 0x27,
          0x2C, 0x2D, 0x2F, 0, 5, 0x10, 0x1C, 0x30, 0x2E, -1)
ENTRIES = ('follow',) * 14 + ('solve',) * 3 + ('prepass',) * 3 + ('heights',) * 2 + \
          ('chase_xz', 'chase_y', 'height', 'height', 'height2', 'height2', 'yaw', 'yaw')
ENTRY_POINT = {'follow': FOLLOW, 'solve': SOLVE, 'prepass': PREPASS, 'heights': HEIGHTS,
               'chase_xz': CHASE_XZ, 'chase_y': CHASE_Y, 'height': HEIGHT, 'height2': HEIGHT2, 'yaw': YAW}


def put(buf, offset, size, value):
    buf[offset:offset + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')


def get_f(buf, offset):
    return struct.unpack_from('<f', buf, offset)[0]


def near(rng, value, spreads=(0.0, 0.5, -0.5, 1.0, -1.0, 1.0000001, -1.0000001, 3.0, -3.0, 20.0, -20.0, 80.0, -80.0)):
    return value + rng.choice(spreads)


def make_case(seed):
    rng = random.Random(seed)
    choose = lambda values: F(rng.choice(values))
    entry = rng.choice(ENTRIES)
    cam = bytearray(rng.getrandbits(8) for _ in range(0xD0))
    player = bytearray(rng.getrandbits(8) for _ in range(0x320))
    wild = rng.random() < 0.05          # leave the float fields as random words
    if not wild:
        for off in (0x10, 0x14, 0x18, 0x20, 0x24, 0x28, 0x30, 0x34, 0x38, 0x94, 0x90, 0x48, 0x4C, 0x40, 0x60):
            put(cam, off, 4, F(rng.uniform(-400, 400)))
        put(cam, 0x0C, 4, choose((-30.0, -46.8, 30.0, -20.0, -60.0, 0.0, -10.0)) if rng.random() < 0.6
            else F(rng.uniform(-80, 80)))
        put(cam, 0x44, 4, angle(rng))
        put(cam, 0x54, 4, choose((1000.0, 200.0, 150.0, 20.0)))
        put(cam, 0x50, 4, choose((-200.0, 100.0, 100.00001, 150.0, 99.99999)))
        put(cam, 0x5C, 4, choose((2.0, 6.0, 0.0)))
        put(cam, 0x8C, 4, choose((6.0, 2.0, 0.0, -3.0, 11.0)))
        put(cam, 0x98, 4, choose((0.0, 23.0)))
        put(cam, 0x94, 4, choose((0.0, 5.0, -3.0)))
        put(cam, 0x64, 4, rng.choice((0xC1F99999, 0xC1F9999A, 0xC23B3333, F(-30.0), F(-46.8), F(-31.2))))
        for off in (0xA0, 0xA4, 0xA8, 0xB0, 0xB4, 0xB8, 0xBC):
            put(player, off, 4, F(rng.uniform(-400, 400)))
        put(player, 0xC4, 4, angle(rng))
        put(player, 0x218, 4, angle(rng))
        put(player, 0x38, 4, choose((0.0, -0.0, 0.3, -0.3, 1e-40)))
    put(cam, 7, 1, rng.choice((0, 0, 0, 1, 2, 4, 8, 9, 0x10, 0x40, 0x80, 0xC0, rng.randrange(256))))
    put(cam, 0x5A, 2, rng.choice((0, 0, 1, 0x80, 0x81, rng.randrange(0x10000))))
    put(cam, 0x6C, 1, rng.choice((0, 0, 1, 0x80)))
    put(cam, 0x6D, 1, rng.choice((0, 1, 0x80, 0x7F)))
    put(cam, 3, 1, rng.choice((0, 0, 1, 2, 7)))
    put(cam, 8, 2, rng.choice((0, 5, 0x1DF, 0x1E0, 0x1E0, 0x1E1, 0x7FFF, 0xFFFF)))
    state = rng.choice(STATES)
    put(player, 0x230, 4, state)
    other = bytearray(player)
    use_other = rng.random() < 0.3
    if use_other:
        # a distinct a1 record: another state and other positions
        put(other, 0x230, 4, rng.choice(STATES))
        for off in (0xA0, 0xA4, 0xA8, 0xB4, 0xC4, 0x38, 0x218):
            if rng.random() < 0.5:
                put(other, off, 4, F(rng.uniform(-3, 3) if off in (0xC4, 0x218) else rng.uniform(-400, 400)))
    g = {'eye': [F(rng.uniform(-400, 400)) for _ in range(3)] + [rng.choice((F(1.0), rng.getrandbits(32)))],
         'target': [F(rng.uniform(-400, 400)) for _ in range(3)] + [rng.getrandbits(32)],
         'area': rng.choice((0xB, 0xB, 0, 3, 4, 0x10, 0x13, rng.randrange(256))),
         'd701': rng.choice((0, 1, 1, 2)), 'd702': rng.choice((0, 1, 2, 3, 4, 5, 6, 7, 8))}
    boom = abs(get_f(cam, 0x0C)) if not wild else 30.0
    if boom != boom or boom > 1e6:
        boom = 30.0
    g['d690'] = F(boom + rng.choice((5.0, 0.5, -0.5, -9.0, -10.0, -10.5, -19.0, -20.5, -30.0, -60.0))) \
        if rng.random() < 0.8 else choose((0.5, 1.0, 0.0, 40.0))
    g['d698'] = choose((10.0, 23.3, 23.299999, 30.0, 23.300001))
    g['d69C'] = choose((5.0, 8.6, 8.6000004, 12.0, -9.0))
    scratch = {name: [rng.getrandbits(32) for _ in range(count)] for name, _, count in SCRATCH}
    scratch['s3B50'] = [angle(rng) for _ in range(4)]
    case = {'seed': seed, 'entry': entry, 'state': state, 'use_other': use_other,
            'freelook': rng.choice((0, 0, 0, 1)), 'a3': rng.getrandbits(32),
            'style': rng.choice((0, 0, 1, 2, 3, 4, 5, 6, 7)), 'mask': rng.choice((6, 7, 0, 0xFF)),
            'globals': g, 'scratch': scratch}
    # entry-specific arguments near their thresholds
    if not wild:
        target = other if use_other else player
        a4 = get_f(target, 0xA4)
        drop = get_f(cam, 0x8C) + 11.0 + get_f(cam, 0x5C) + a4 + get_f(cam, 0x98)
        if rng.random() < 0.5:
            put(cam, 0x14, 4, F(near(rng, drop)))
        if rng.random() < 0.3:
            put(cam, 0x54, 4, F(near(rng, drop, (0.0, -1.0, 1.0, -5.0))))
        if rng.random() < 0.3:
            put(cam, 0x14, 4, choose((250.0, 250.00002, 300.0, 120.0)))
            put(cam, 0x10, 4, choose((356.0, 355.99997, 100.0, 400.0)))
        if rng.random() < 0.2:
            put(cam, 0x18, 4, choose((-1449.0, -1449.0001, -1500.0, 0.0)))
    y14 = get_f(cam, 0x14) if not wild else 0.0
    y14 = y14 if y14 == y14 and abs(y14) < 1e6 else 0.0
    y98 = get_f(cam, 0x98) if not wild else 0.0
    y98 = y98 if y98 == y98 and abs(y98) < 1e6 else 0.0
    case['y'] = F(near(rng, y14 - y98)) if rng.random() < 0.8 else F(rng.uniform(-400, 400))
    case['rate'] = choose((1.0, 4.0, 3.0, 0.5, 0.1))
    case['up'], case['down'] = choose((15.0, 25.0, 0.0, 0.5)), choose((10.0, 20.0, 0.0, 0.5))
    src = [F(rng.uniform(-400, 400)) for _ in range(3)]
    dst = [F(number(src[i]) + rng.choice((0.0, 0.5, -0.5, 1.0, -1.0, 6.0, -6.0, 30.0, -30.0, 100.0)))
           if rng.random() < 0.8 else F(rng.uniform(-400, 400)) for i in range(3)]
    case['src'], case['dst'] = src, dst
    case['max'] = choose((4.0, 0.8, 1.8, 1.0, 0.5, 100.0))
    goal = rng.uniform(-3.2, 3.2)
    case['goal'] = F(goal)
    case['current'] = F(goal + rng.choice((0.0, 0.01, -0.01, 0.0349, -0.0349, 0.5, -0.5, 1.0, -1.0, 2.0)))
    case['yaw_rate'] = choose((0.034906585, 0.0052359877, 0.5))
    case['limit'] = choose((0.7853982, 0.0, 3.0))
    case['cam'], case['player'], case['other'] = bytes(cam), bytes(player), bytes(other)
    return case


# ======================================================================
# The oracle
# ======================================================================

class UnitOracle:
    def __init__(self, elf):
        self.ee = CameraEE(elf, cover=True)
        for address, name in CALLEES.items():
            self.ee.hooks[address] = self.hook(name)

    def hook(self, name):
        def run(ee):
            entry = self.log_entry(name, ee)
            self.log.append(entry)
            _, e = self.script.next(name, entry)
            ee.in_hook = True
            for address, size, value in e['writes']:
                ee.save(address, value, size)
            if e['hit'] is not None:
                ee.save(HIT_POINTER, HIT_RECORD)
                ee.save(HIT_RECORD + 0x1A, e['hit'][0], 2)
                ee.save(HIT_Y, e['hit'][1])
            if e['out'] is not None:
                for i, value in enumerate(e['out']):
                    ee.save(ee.arg(0) + 4 * i, value)
            ee.in_hook = False
            if e['fret'] is not None:
                ee.f[0] = e['fret']
            ee.ret_int(e['ret'])
        return run

    def log_entry(self, name, ee):
        a = ee.arg
        f = lambda i: ee.f[12 + i] & MASK
        vec = lambda address, n: tuple(ee.load(address + 4 * i) for i in range(n))
        if name == 'approach': return (name, f(0), f(1), f(2))
        if name in ('wrap', 'sine', 'cosine'): return (name, f(0))
        if name == 'heading': return (name, vec(a(0), 3), f(0), f(1))
        if name == 'tether':
            assert a(0) == CAM, ('tether cam', hex(a(0)))
            return (name, {PLAYER: 'player', OTHER: 'other'}[a(1)])
        if name in ('solve', 'solve_aim', 'bounds'):
            assert a(0) == CAM and a(1) == PLAYER, (name, hex(a(0)), hex(a(1)))
            return (name, s32(a(2)), s32(a(3))) if name != 'bounds' else (name, s32(a(2)))
        if name == 'segment': return (name, vec(a(0), 4), vec(a(1), 4), s32(a(2)))
        if name == 'ground': return (name, vec(a(0), 4), vec(a(1), 4))
        if name == 'identity':
            assert a(0) == 0x70003400, ('identity out', hex(a(0)))
            return (name,)
        if name == 'euler':
            assert (a(0), a(1), a(2)) == (0x70003400, 0x70003400, CAM + 0x30), ('euler pointers',)
            return (name, vec(a(1), 16), vec(a(2), 4))
        raise AssertionError(name)

    def load_case(self, case):
        ee = self.ee
        ee.write(CAM, case['cam'])
        ee.write(PLAYER, case['player'])
        ee.write(OTHER, case['other'])
        g = case['globals']
        for name, address, count in GLOBAL_WORDS:
            values = g[name] if count > 1 else [g[name]]
            for i, value in enumerate(values): ee.save(address + 4 * i, value)
        for name, address in GLOBAL_BYTES:
            ee.save(address, g[name], 1)
        for name, address, count in SCRATCH:
            for i, value in enumerate(case['scratch'][name]): ee.save(address + 4 * i, value)
        ee.save(HIT_POINTER, 0x6F0000)          # a stale hit record
        ee.save(0x6F0000 + 0x1A, 0xFFFF, 2)
        ee.save(HIT_Y, F(-777.0))

    def run(self, case, script):
        ee = self.ee
        self.script, self.log = script, []
        ee.r, ee.rh = [0] * 32, [0] * 32
        ee.f, ee.acc, ee.cond = [0] * 32, 0, False
        ee.vf = [[0, 0, 0, 0] for _ in range(32)]
        ee.vf[0][3] = F(1.0)
        ee.vacc, ee.q = [0, 0, 0, 0], 0
        ee.r[28], ee.r[29] = 0x27D370, shared.STACK_TOP
        self.load_case(case)
        ee.writes = set()
        a1 = OTHER if case['use_other'] else PLAYER
        entry = case['entry']
        c = case
        if entry == 'follow':
            ee.call(FOLLOW, (CAM, a1, c['freelook'], c['a3']))
        elif entry == 'solve':
            ee.call(SOLVE, (CAM, c['style']))
        elif entry == 'prepass':
            ee.call(PREPASS, (CAM, a1, c['style'], c['mask']))
        elif entry == 'heights':
            ee.call(HEIGHTS, (CAM, a1))
        elif entry == 'chase_xz':
            for i in range(3):
                ee.save(CAM + 0x10 + 4 * i, c['src'][i]); ee.save(0x8105D0 + 4 * i, c['dst'][i])
            ee.writes = set()
            ee.f[12] = c['max']
            ee.call(CHASE_XZ, (CAM + 0x10, 0x8105D0))
        elif entry == 'chase_y':
            for i in range(3): ee.save(0x8105D0 + 4 * i, c['dst'][i])
            ee.writes = set()
            ee.f[12], ee.f[13] = c['y'], c['max']
            ee.call(CHASE_Y, (0x8105D0,))
        elif entry == 'height':
            ee.f[12], ee.f[13] = c['y'], c['rate']
            ee.call(HEIGHT, (CAM,))
        elif entry == 'height2':
            ee.f[12], ee.f[13], ee.f[14] = c['y'], c['up'], c['down']
            ee.call(HEIGHT2, (CAM,))
        elif entry == 'yaw':
            ee.f[12], ee.f[13], ee.f[14], ee.f[15] = c['goal'], c['current'], c['yaw_rate'], c['limit']
            ee.call(YAW, ())
        written = ee.writes
        ee.writes = None
        stray = sorted(a for a in written if a not in COMPARED)
        assert not stray, (case['seed'], entry, 'original writes outside the compared set',
                           [hex(a) for a in stray[:16]])
        result = {'v0': s32(ee.r[2]), 'f0': ee.f[0] & MASK}
        return {'image': ee_image(ee), 'log': self.log, 'result': result}


class NativeRun:
    """em_camera_follow_original.c with Python workers replaying the script."""

    def __init__(self, native, case, script, missing=None):
        self.native, self.case, self.script, self.log = native, case, script, []
        s = self.state = NativeState()
        C.memmove(s.cam.bytes, case['cam'], 0xD0)
        C.memmove(s.player.bytes, case['player'], 0x320)
        C.memmove(s.other.bytes, case['other'], 0x320)
        g = case['globals']
        for name, _, count in GLOBAL_WORDS:
            for i, value in enumerate(g[name] if count > 1 else [g[name]]): s.g[name][i] = value
        for name, _ in GLOBAL_BYTES:
            s.g[name][0] = g[name]
        for name, _, count in SCRATCH:
            field = getattr(s.scratch, name)
            for i in range(count): field[i] = case['scratch'][name][i]
        self.keep = []
        fields = {}
        for name in WORKER_FIELDS:
            fields[name] = FN[name](self.worker(name)) if name != missing else FN[name]()
            self.keep.append(fields[name])
        self.workers = Workers(None, **fields)
        self.world = World(C.pointer(s.cam), C.pointer(s.player), C.pointer(s.globals),
                           C.pointer(s.scratch), C.pointer(self.workers))
        if missing in ('cam', 'player', 'globals', 'scratch', 'workers'):
            setattr(self.world, missing, type(getattr(self.world, missing))())
        elif missing is not None and missing.startswith('g:'):
            name = missing[2:]
            setattr(s.globals, name, type(getattr(s.globals, name))())

    def call(self, name, entry):
        self.log.append(entry)
        index, e = self.script.next(name, entry)
        if self.script.fail_at == index:
            return None
        for address, size, value in e['writes']:
            self.state.save(address, value, size)
        return e

    def which(self, pointer, what):
        address = C.addressof(pointer.contents)
        s = self.state
        table = {C.addressof(s.player): 'player', C.addressof(s.other): 'other', C.addressof(s.cam): 'cam'}
        assert address in table, (what, 'pointer is none of the records')
        return table[address]

    def worker(self, name):
        vec = lambda pointer, n: tuple(pointer[i] for i in range(n))

        def fret(e, out):
            out[0] = e['fret']
            return 0

        if name == 'approach':
            def fn(_, target, current, rate, out):
                e = self.call(name, (name, target, current, rate))
                return -1 if e is None else fret(e, out)
            return fn
        if name in ('wrap', 'sine', 'cosine'):
            def fn(_, x, out):
                e = self.call(name, (name, x))
                return -1 if e is None else fret(e, out)
            return fn
        if name == 'heading':
            def fn(_, obj, x, z, out):
                e = self.call(name, (name, vec(obj, 3), x, z))
                return -1 if e is None else fret(e, out)
            return fn
        if name == 'tether':
            def fn(_, cam, player):
                assert self.which(cam, 'tether cam') == 'cam'
                e = self.call(name, (name, self.which(player, 'tether player')))
                return -1 if e is None else 0
            return fn
        if name in ('solve', 'solve_aim'):
            def fn(_, cam, player, style, mask, out):
                assert self.which(cam, name) == 'cam' and self.which(player, name) == 'player', name
                e = self.call(name, (name, style, mask))
                if e is None: return -1
                out[0] = e['ret']
                return 0
            return fn
        if name == 'bounds':
            def fn(_, cam, player, mask):
                assert self.which(cam, name) == 'cam' and self.which(player, name) == 'player', name
                return -1 if self.call(name, (name, mask)) is None else 0
            return fn
        if name == 'segment':
            def fn(_, a, b, mask, hit):
                e = self.call(name, (name, vec(a, 4), vec(b, 4), mask))
                if e is None: return -1
                hit[0].result = e['ret']
                if e['ret'] != 0:
                    hit[0].record_1A, hit[0].point_y = e['hit']
                return 0
            return fn
        if name == 'ground':
            def fn(_, a, b, out):
                e = self.call(name, (name, vec(a, 4), vec(b, 4)))
                if e is None: return -1
                out[0] = e['ret']
                return 0
            return fn
        if name == 'identity':
            def fn(_, m):
                assert C.addressof(m.contents) == C.addressof(self.state.scratch.s3400), 'identity out'
                e = self.call(name, (name,))
                if e is None: return -1
                for i in range(16): m[i] = e['out'][i]
                return 0
            return fn
        if name == 'euler':
            def fn(_, out, m, angles):
                assert C.addressof(out.contents) == C.addressof(self.state.scratch.s3400), 'euler out'
                e = self.call(name, (name, vec(m, 16), vec(angles, 4)))
                if e is None: return -1
                for i in range(16): out[i] = e['out'][i]
                return 0
            return fn
        raise AssertionError(name)

    def run(self):
        n, c, s = self.native, self.case, self.state
        W = C.byref(self.world)
        a1 = C.byref(s.other if c['use_other'] else s.player)
        entry = c['entry']
        v0, f0 = None, None
        if entry == 'follow':
            status = n.em_camera_follow_001921D0(W, a1, c['freelook'])
        elif entry == 'solve':
            out = C.c_int(-99)
            status = n.em_camera_follow_0018D7B0(W, c['style'], C.byref(out))
            v0 = out.value
        elif entry == 'prepass':
            status = n.em_camera_follow_0018D330(W, a1, c['style'], c['mask'])
        elif entry == 'heights':
            status = n.em_camera_follow_00191390(C.byref(s.cam), a1)
        elif entry == 'chase_xz':
            for i in range(3):
                s.save(CAM + 0x10 + 4 * i, c['src'][i]); s.save(0x8105D0 + 4 * i, c['dst'][i])
            src = (U32 * 3)(*c['src'])
            out = C.c_int(-99)
            status = n.em_camera_follow_0018C6A0(src, s.g['eye'], c['max'], C.byref(out))
            v0 = out.value
        elif entry == 'chase_y':
            for i in range(3): s.save(0x8105D0 + 4 * i, c['dst'][i])
            out = C.c_int(-99)
            status = n.em_camera_follow_0018C4B0(s.g['eye'], c['y'], c['max'], C.byref(out))
            v0 = out.value
        elif entry == 'height':
            status = n.em_camera_follow_00191D40(C.byref(s.cam), C.byref(s.globals), c['y'], c['rate'])
        elif entry == 'height2':
            status = n.em_camera_follow_00192010(C.byref(s.cam), c['y'], c['up'], c['down'])
        elif entry == 'yaw':
            out = U32(0)
            status = n.em_camera_follow_00191120(C.byref(self.workers), c['goal'], c['current'],
                                                 c['yaw_rate'], c['limit'], C.byref(out))
            f0 = out.value
        return status, {'image': s.image(), 'log': self.log, 'v0': v0, 'f0': f0}


ELF = NATIVE = ORACLE = None


def run_case(seed):
    global ORACLE
    if ORACLE is None:
        ORACLE = UnitOracle(ELF)
    case = make_case(seed)
    want = ORACLE.run(case, Script(seed))
    status, got = NativeRun(NATIVE, case, Script(seed)).run()
    where = (seed, case['entry'], hex(case['state']), case['use_other'])
    assert status == 0, (where, 'native fault', status)
    assert want['log'] == got['log'], (where, 'worker calls', want['log'], got['log'])
    if want['image'] != got['image']:
        raise AssertionError((where, 'state differs at', image_diff(want['image'], got['image'])[:24]))
    if got['v0'] is not None:
        assert want['result']['v0'] == got['v0'], (where, 'return', want['result']['v0'], got['v0'])
    if got['f0'] is not None:
        assert want['result']['f0'] == got['f0'], (where, 'f0', hex(want['result']['f0']), hex(got['f0']))
    faults = 0
    if want['log'] and seed % 4 == 0 and case['entry'] in ('follow', 'solve', 'prepass', 'yaw'):
        k = random.Random(seed).randrange(len(want['log']))
        status, cut = NativeRun(NATIVE, case, Script(seed, fail_at=k)).run()
        assert status == -1, (where, 'fault not reported', k)
        assert cut['log'] == want['log'][:k + 1], (where, 'calls after a fault', k)
        faults = 1
    dispatch = (case['entry'], case['state'] if case['entry'] in ('follow', 'heights') else case['style'])
    return (dispatch, len(want['log']), faults, tuple(sorted(ORACLE.ee.outcomes)))


def missing_worker_checks(native):
    """Each worker, record or table missing: the world entry points return
    -1 before any write and call nothing."""
    count = 0
    base = make_case(1000)
    for missing in WORKER_FIELDS + ('cam', 'player', 'globals', 'scratch', 'workers') + \
            tuple('g:' + name for name, _, _ in GLOBAL_WORDS) + tuple('g:' + name for name, _ in GLOBAL_BYTES):
        for entry in ('follow', 'solve', 'prepass'):
            case = dict(base, entry=entry)
            run = NativeRun(native, case, Script(1000), missing=missing)
            before = run.state.image()
            status, got = run.run()
            assert status == -1 and got['log'] == [] and got['image'] == before, (missing, entry)
            count += 1
    # 00191120 needs only the angle wrap; 00191D40 only area / d701 / d702
    case = dict(base, entry='yaw')
    run = NativeRun(native, case, Script(1000), missing='wrap')
    status, got = run.run()
    assert status == -1 and got['log'] == [], 'yaw without wrap'
    count += 1
    for name in ('area', 'd701', 'd702'):
        run = NativeRun(native, dict(base, entry='height'), Script(1000), missing='g:' + name)
        before = run.state.image()
        status, got = run.run()
        assert status == -1 and got['image'] == before, ('00191D40 without', name)
        count += 1
    return count


def main():
    global ELF, NATIVE
    started = time.time()
    ELF = read_elf()
    callees = check_callee_set(ELF)
    NATIVE = build_native()
    total = reference_mode.pick(60000, 60000)
    seeds = reference_mode.select(range(total), 4000, 0xCA3E)
    results = reference_mode.parallel_map(run_case, seeds)
    outcomes, dispatch, faults, calls = set(), {}, 0, 0
    for key, count, fault, cover in results:
        outcomes.update(cover)
        dispatch[key] = dispatch.get(key, 0) + 1
        faults += fault
        calls += count
    sites = branch_sites(ELF)
    missing = sorted('%06X %s' % (pc, 'taken' if taken else 'not taken')
                     for pc in sites for taken in (True, False) if (pc, taken) not in outcomes)
    assert not missing, ('branch outcomes never exercised', missing)
    states = {s & MASK for s in STATES}
    ran = {k[1] & MASK for k in dispatch if k[0] == 'follow'}
    assert states <= ran, ('player states never run through 001921D0', sorted(states - ran))
    styles = {k[1] for k in dispatch if k[0] == 'solve'}
    assert styles >= set(range(8)), ('0018D7B0 styles never run', sorted(set(range(8)) - styles))
    stops = missing_worker_checks(NATIVE)
    captured = captured_main(ELF, NATIVE)
    entries = {}
    for (entry, _), n in dispatch.items():
        entries[entry] = entries.get(entry, 0) + n
    reference_mode.banner(reference_mode.part(len(seeds), total, 'cases'),
                          '%d jal targets (all hooked or translated)' % callees)
    print('camera follow vs original instructions: PASS %d cases (%s), %d worker calls identical, '
          'every one of %d conditional branches both ways, %d fault-stop cuts, %d missing-worker '
          'refusals; %s (%.1fs)' % (
              len(seeds), ', '.join('%s %d' % kv for kv in sorted(entries.items())), calls,
              len(sites), faults, stops, captured, time.time() - started))


# ======================================================================
# Captured RAM: the original camera frame with the translations in place
# ======================================================================

VEC = 0x7F0E0000                 # private vectors for worker arguments (stack region)
HOOKED = {FOLLOW: 'follow', SOLVE: 'solve', PREPASS: 'prepass', HEIGHTS: 'heights',
          CHASE_XZ: 'chase_xz', CHASE_Y: 'chase_y', HEIGHT: 'height', HEIGHT2: 'height2', YAW: 'yaw'}


class WorldBinding:
    """EmCameraFollowWorkers bound to the ORIGINAL callees executed in the same
    EE on the same world; the native records, globals and scratch are synced
    into the EE around every call and back out after it."""

    def __init__(self, ee):
        self.ee = ee
        self.state = NativeState()
        self.error = None
        self.keep = []
        fields = {}
        for name in WORKER_FIELDS:
            fields[name] = FN[name](self.guard(getattr(self, 'w_' + name)))
            self.keep.append(fields[name])
        self.workers = Workers(None, **fields)
        s = self.state
        self.world = World(C.pointer(s.cam), C.pointer(s.player), C.pointer(s.globals),
                           C.pointer(s.scratch), C.pointer(self.workers))

    def guard(self, fn):
        def run(*args):
            if self.error is not None:
                return -1
            try:
                return fn(*args[1:])
            except BaseException as error:        # surfaced after the native call returns
                self.error = error
                return -1
        return run

    SYNCED = sorted(a for a in COMPARED if not OTHER <= a < OTHER + 0x320)

    def sync_in(self):
        """EE -> native."""
        ee, s = self.ee, self.state
        C.memmove(s.cam.bytes, ee.read(CAM, 0xD0), 0xD0)
        C.memmove(s.player.bytes, ee.read(PLAYER, 0x320), 0x320)
        for name, address, count in GLOBAL_WORDS:
            for i in range(count): s.g[name][i] = ee.load(address + 4 * i)
        for name, address in GLOBAL_BYTES:
            s.g[name][0] = ee.load(address, 1)
        for name, address, count in SCRATCH:
            field = getattr(s.scratch, name)
            for i in range(count): field[i] = ee.load(address + 4 * i)

    def sync_out(self):
        """native -> EE."""
        ee, s = self.ee, self.state
        ee.write(CAM, bytes(s.cam.bytes))
        ee.write(PLAYER, bytes(s.player.bytes))
        for name, address, count in GLOBAL_WORDS:
            for i in range(count): ee.save(address + 4 * i, s.g[name][i])
        for name, address in GLOBAL_BYTES:
            ee.save(address, s.g[name][0], 1)
        for name, address, count in SCRATCH:
            field = getattr(s.scratch, name)
            for i in range(count): ee.save(address + 4 * i, field[i])

    def call(self, entry, args=(), fregs=()):
        self.sync_out()
        v0, f0 = nested_bits(self.ee, entry, args, fregs)
        self.sync_in()
        return s32(v0), f0 & MASK

    def put_vec(self, slot, values):
        for i, value in enumerate(values): self.ee.save(VEC + 0x40 * slot + 4 * i, value)
        return VEC + 0x40 * slot

    def record_address(self, pointer):
        address = C.addressof(pointer.contents)
        if address == C.addressof(self.state.cam): return CAM
        if address == C.addressof(self.state.player): return PLAYER
        raise AssertionError('worker record pointer is neither the camera nor the player')

    # ---- the workers (EmCameraFollowWorkers order) ------------------------
    def w_approach(self, t, c, r, out): out[0] = self.call(0x1B12B0, (), (t, c, r))[1]; return 0
    def w_wrap(self, x, out): out[0] = self.call(0x1B1470, (), (x,))[1]; return 0
    def w_sine(self, x, out): out[0] = self.call(0x11E2A8, (), (x,))[1]; return 0
    def w_cosine(self, x, out): out[0] = self.call(0x11DE90, (), (x,))[1]; return 0

    def w_heading(self, obj, x, z, out):
        # 001B1240 reads obj[0] and obj[2] only; the translation hands a copy
        address = self.put_vec(0, [obj[i] for i in range(3)] + [0])
        out[0] = self.call(0x1B1240, (address,), (x, z))[1]; return 0

    def w_tether(self, cam, player):
        self.call(0x230000, (self.record_address(cam), self.record_address(player))); return 0

    def w_solve(self, cam, player, style, mask, out):
        out[0] = self.call(0x18DD20, (self.record_address(cam), self.record_address(player), style, mask))[0]; return 0

    def w_solve_aim(self, cam, player, style, mask, out):
        out[0] = self.call(0x18F870, (self.record_address(cam), self.record_address(player), style, mask))[0]; return 0

    def w_bounds(self, cam, player, mask):
        self.call(0x18D910, (self.record_address(cam), self.record_address(player), mask)); return 0

    def w_segment(self, a, b, mask, hit):
        v0 = self.call(0x19A910, (self.put_vec(0, [a[i] for i in range(4)]),
                                  self.put_vec(1, [b[i] for i in range(4)]), mask))[0]
        hit[0].result = v0
        if v0 != 0:
            record = self.ee.load(HIT_POINTER)
            hit[0].record_1A = self.ee.load(record + 0x1A, 2)
            hit[0].point_y = self.ee.load(HIT_Y)
        return 0

    def w_ground(self, a, b, out):
        out[0] = self.call(0x19B7D0, (self.put_vec(0, [a[i] for i in range(4)]),
                                      self.put_vec(1, [b[i] for i in range(4)])))[0]; return 0

    def w_identity(self, m):
        assert C.addressof(m.contents) == C.addressof(self.state.scratch.s3400)
        self.call(0x1029C0, (0x70003400,)); return 0

    def w_euler(self, out, m, angles):
        assert C.addressof(out.contents) == C.addressof(self.state.scratch.s3400)
        cam30 = list(struct.unpack_from('<4I', bytes(self.state.cam.bytes), 0x30))
        assert [angles[i] for i in range(4)] == cam30, 'euler angles are not cam+30'
        self.call(0x102C58, (0x70003400, 0x70003400, CAM + 0x30)); return 0


DEPTH = [0]     # hooked routines active: only outermost calls are counted


def native_hook(native, kind, counts):
    def hook(ee):
        top = DEPTH[0] == 0
        DEPTH[0] += 1
        try:
            native_call(native, kind, ee)
        finally:
            DEPTH[0] -= 1
        if top:
            counts[kind] = counts.get(kind, 0) + 1
    return hook


def native_call(native, kind, ee):
    assert kind in ('chase_xz', 'chase_y', 'yaw') or ee.arg(0) == CAM, (kind, 'camera record', hex(ee.arg(0)))
    binding = WorldBinding(ee)
    binding.sync_in()
    s = binding.state
    W = C.byref(binding.world)
    v0 = f0 = None
    if kind in ('follow', 'prepass', 'heights'):
        assert ee.arg(1) == PLAYER, (kind, 'a1', hex(ee.arg(1)))
    if kind == 'follow':
        status = native.em_camera_follow_001921D0(W, C.byref(s.player), s32(ee.arg(2)))
    elif kind == 'solve':
        out = C.c_int(-99)
        status = native.em_camera_follow_0018D7B0(W, s32(ee.arg(1)), C.byref(out))
        v0 = out.value
    elif kind == 'prepass':
        status = native.em_camera_follow_0018D330(W, C.byref(s.player), s32(ee.arg(2)), s32(ee.arg(3)))
    elif kind == 'heights':
        status = native.em_camera_follow_00191390(C.byref(s.cam), C.byref(s.player))
    elif kind in ('chase_xz', 'chase_y'):
        # the vectors live wherever the caller points: operate on copies
        # and write the destination back
        out = C.c_int(-99)
        if kind == 'chase_xz':
            src = (U32 * 3)(*[ee.load(ee.arg(0) + 4 * i) for i in range(3)])
            dst = (U32 * 3)(*[ee.load(ee.arg(1) + 4 * i) for i in range(3)])
            assert native.em_camera_follow_0018C6A0(src, dst, ee.f[12] & MASK, C.byref(out)) == 0
            for i in (0, 2): ee.save(ee.arg(1) + 4 * i, dst[i])
        else:
            v = (U32 * 3)(*[ee.load(ee.arg(0) + 4 * i) for i in range(3)])
            assert native.em_camera_follow_0018C4B0(v, ee.f[12] & MASK, ee.f[13] & MASK, C.byref(out)) == 0
            ee.save(ee.arg(0) + 4, v[1])
        ee.ret_int(out.value)
        return
    elif kind == 'height':
        status = native.em_camera_follow_00191D40(C.byref(s.cam), C.byref(s.globals), ee.f[12] & MASK,
                                                  ee.f[13] & MASK)
    elif kind == 'height2':
        status = native.em_camera_follow_00192010(C.byref(s.cam), ee.f[12] & MASK, ee.f[13] & MASK,
                                                  ee.f[14] & MASK)
    else:
        out = U32(0)
        status = native.em_camera_follow_00191120(C.byref(binding.workers), ee.f[12] & MASK,
                                                  ee.f[13] & MASK, ee.f[14] & MASK, ee.f[15] & MASK,
                                                  C.byref(out))
        f0 = out.value
    if binding.error is not None:
        raise binding.error
    assert status == 0, (kind, 'native fault', status)
    binding.sync_out()
    if v0 is not None: ee.ret_int(v0)
    if f0 is not None: ee.f[0] = f0


def frame_digest(ee):
    digest = hashlib.sha1(ee.mem)
    digest.update(ee.spad)
    return digest.hexdigest()


CAPTURES = {}


def captured_sources():
    """(name, RAM, scratchpad) of every captured image in follow mode."""
    import json
    out = []
    reason = shared.world_inputs()
    if reason is None:
        out.append(('state04', shared.WORLD_RAM.read_bytes(), shared.WORLD_SPAD.read_bytes()))
    seen = set()
    if shared.ROUTE.exists():
        for beat in sorted(os.listdir(shared.ROUTE)):
            trace = shared.ROUTE / beat / 'trace.json'
            if not trace.exists():
                continue
            source = json.loads(trace.read_text())['source']
            files = [shared.ROUTE / source / n for n in ('eeMemory.bin', 'scratchpad.bin')]
            if source in seen or not all(f.exists() for f in files):
                continue
            seen.add(source)
            out.append((source, files[0].read_bytes(), files[1].read_bytes()))
    return out


def captured_job(index):
    name, ram, spad = CAPTURES['sources'][index]
    results = []
    for native_mode in (False, True):
        ee = CameraEE(CAPTURES['elf'], ram, spad)
        counts = {}
        reached = {}
        if native_mode:
            for address, kind in HOOKED.items():
                ee.hooks[address] = native_hook(CAPTURES['native'], kind, counts)
        else:
            for address, kind in HOOKED.items():
                ee.hooks[address] = counting_hook(address, kind, reached)
        ee.call(CAMERA_FRAME, (CAM,))
        results.append((frame_digest(ee), counts if native_mode else reached,
                        ee.read(CAM, 0xD0), ee.read(0x8105D0, 0x40)))
    return name, results


def counting_hook(address, kind, reached):
    """Runs the original routine itself (the hook only counts the call)."""
    def hook(ee):
        if DEPTH[0] == 0:
            reached[kind] = reached.get(kind, 0) + 1
        DEPTH[0] += 1
        saved = ee.hooks.pop(address)
        try:
            v0, f0 = nested_bits(ee, address, tuple(ee.r[4 + i] & MASK for i in range(4)),
                                 tuple(ee.f[12 + i] & MASK for i in range(4)))
        finally:
            ee.hooks[address] = saved
            DEPTH[0] -= 1
        ee.r[2], ee.f[0] = v0, f0
    return hook


def captured_main(elf, native):
    sources = captured_sources()
    if not sources:
        return 'captured check skipped (no captured RAM; docs/CAMERA_FOLLOW_ORIGINAL.md)'
    CAPTURES.update(elf=elf, native=native, sources=sources)
    results = reference_mode.parallel_map(captured_job, range(len(sources)))
    reached_total = {}
    for name, ((a_hash, reached, a_cam, a_eye), (b_hash, counts, b_cam, b_eye)) in results:
        if a_cam != b_cam:
            raise AssertionError((name, 'camera record differs at',
                                  [hex(k) for k in range(0xD0) if a_cam[k] != b_cam[k]]))
        assert a_eye == b_eye, (name, 'camera globals differ')
        assert a_hash == b_hash, (name, 'RAM or scratchpad differs outside the camera record')
        assert reached == counts, (name, 'the translations ran a different call set', reached, counts)
        for kind, n in counts.items():
            reached_total[kind] = reached_total.get(kind, 0) + n
    assert reached_total.get('follow', 0) > 0, ('no captured image reaches 001921D0', reached_total)
    return 'captured: %d images through 0018B9C0 identical (whole RAM + scratchpad), native calls %s' % (
        len(sources), ', '.join('%s %d' % kv for kv in sorted(reached_total.items())))


# ======================================================================
# World mode: the route beats replayed with the camera frame every frame
# ======================================================================

class CameraRoute(shared.RouteReplay):
    """RouteReplay on CameraEE: the original player stage per frame as
    test_player_slide_reference.RouteReplay drives it (pad, camera globals
    from the previous trace row, counters). The camera frame is run by
    beat_replay after it (the original order: player stage, then camera)."""

    def __init__(self, elf, trace, ram, spad):
        self.ee = ee = CameraEE(elf, ram, spad)
        self.frame, self.events = 0, []
        for address, name in shared.SOUND_HOOKS.items():
            ee.hooks[address] = self.recorder(name)
        ee.hooks[shared.PAD_READ] = self.pad_read
        self.raw = bytes(8)
        self.rows = {r['counter']: r for r in trace['rows']}
        self.first = trace['first_counter']
        self.inputs = sorted(trace['inputs'], key=lambda i: i['f'])
        self.counter = ee.load(0x70003B64)


# beat -> trace frames replayed (None: the whole beat). The replay drives only
# the player stage and the camera frame (no scripts or owners), so it follows
# the recorded play only while nothing else drives the player or the camera
# (docs/CAMERA_FOLLOW_ORIGINAL.md "Verification").
WORLD_BEATS = {'05_boxes': None, '06_hill_slide': None, '12_crevice_jump': None,
               '10_cage_roof_roger': 400}
WORLD = {}


def beat_replay(job):
    """Per frame: the player stage, then the camera frame twice from the same
    RAM and scratchpad, once all original and once with the translations in
    place; the two results must be identical, and the original one carries
    on to the next frame."""
    beat, last = job
    trace, ram, spad = WORLD[beat]
    replay = CameraRoute(WORLD['elf'], trace, ram, spad)
    ee = replay.ee
    counts, reached, errors, states = {}, {}, [], {}
    original = {address: counting_hook(address, kind, reached) for address, kind in HOOKED.items()}
    native = {address: native_hook(WORLD['native'], kind, counts) for address, kind in HOOKED.items()}
    frames = 0
    end = trace['first_counter'] + last
    while replay.counter < end:
        row = replay.step()
        mem, spad_before = bytearray(ee.mem), bytearray(ee.spad)
        state = s32(ee.load(PLAYER + 0x230))
        ee.hooks.update(native)
        ee.call(CAMERA_FRAME, (CAM,))
        translated = frame_digest(ee)
        ee.mem[:], ee.spad[:] = mem, spad_before
        ee.hooks.update(original)
        ee.call(CAMERA_FRAME, (CAM,))
        assert frame_digest(ee) == translated, (beat, replay.counter, 'RAM or scratchpad differs')
        frames += 1
        if ee.load(CAM + 4, 1) == 0:
            states[state] = states.get(state, 0) + 1
        if row is not None and row['cam_mode'][:2] == '00':
            eye = [number(ee.load(0x8105D0 + 4 * i)) for i in range(3)]
            errors.append(max(abs(eye[i] - row['eye'][i]) for i in range(3)))
    return frames, counts, reached, errors, states


def world_main():
    elf = read_elf()
    beats = [b for b in os.environ.get('EM_WORLD_BEATS', ','.join(WORLD_BEATS)).split(',') if b]
    for beat in beats:
        loaded = shared.route_beat(beat)
        if isinstance(loaded, str):
            raise SystemExit('world mode: %s (docs/CAMERA_FOLLOW_ORIGINAL.md)' % loaded)
        WORLD[beat] = loaded
    WORLD['elf'], WORLD['native'] = elf, build_native()
    cap = int(os.environ.get('EM_WORLD_FRAMES', '0') or 0)
    jobs = []
    for beat in beats:
        trace = WORLD[beat][0]
        length = WORLD_BEATS.get(beat) or max(r['counter'] for r in trace['rows']) - trace['first_counter']
        jobs.append((beat, min(length, cap) if cap else length))
    started = time.time()
    results = reference_mode.parallel_map(beat_replay, jobs, cost=lambda job: job[1])
    for (beat, _), (frames, counts, reached, errors, states) in zip(jobs, results):
        assert reached == counts, (beat, 'the translations ran a different call set', reached, counts)
        assert counts.get('follow', 0) > 0, (beat, 'the replay never reached 001921D0', counts)
        errors.sort()
        spread = ('actual eye vs trace rows in follow mode: median %.3g, max %.3g over %d rows' %
                  (errors[len(errors) // 2], errors[-1], len(errors))) if errors else 'no follow rows'
        print('%s: PASS %d frames identical (whole RAM + scratchpad), native calls %s; player '
              'states +230 in follow %s; %s' % (
                  beat, frames, ', '.join('%s %d' % kv for kv in sorted(counts.items())),
                  ' '.join('%X:%d' % kv for kv in sorted(states.items())), spread))
    print('camera follow world mode: PASS (%.0fs)' % (time.time() - started))


if __name__ == '__main__':
    if os.environ.get('EM_TEST_WORLD', '') not in ('', '0'):
        world_main()
    else:
        main()
