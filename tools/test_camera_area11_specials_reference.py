#!/usr/bin/env python3
"""Execute the original camera specials and compare em_camera_area11_specials.c.

docs/CAMERA_AREA11_SPECIALS.md. The user's pinned ELF (and, for the world
mode, the captured route RAM) supplies every instruction; none are embedded
here. Routines executed unmodified (the translated set):

  00195130  camera action 0: the per-area walking camera (AREA11 = area 0xB)
  00193EB0  the event router (L1 orient-behind gate, reactions, region events)
  001936E0  camera action 3: the melee lock-on swing
  00197490  the aim release
  00191210  the area-0x10 eye clamp
  and the leaves the translation inlines: 0011DF78, 00102948, 001031E0,
  001029C0, 001028D0, 00102738.

Every other callee is hooked, scripted per case and recorded (never
simulated as a claim about the callee); the native module gets the same
script through its workers. The test asserts that the hooked set is exactly
the set of jal targets of the translated routines, so no callee runs
unhooked, and that every store the original makes lands in the compared
state (the 0x810000..0x810FFF window holding the camera block, the player
record, the eye/target and every global the routines read, plus the
scratchpad words EmCamSpecialsScratch mirrors). At every worker call the
whole compared state is digested on both sides, so an intermediate
difference fails even when a later write hides it.

Arithmetic: CamEE is the fall oracle's FallEE (COP1 and VU0 macro ops
through tools/ee_float_model.py, docs/EE_FLOAT_MODEL.md); nothing shared is
edited.

Default run (~10 s): unit cases with every conditional branch of the five
routines exercised both ways (asserted), fail-stop cuts, missing-worker
refusals, and the captured route snapshots run once each (world mode, as-is
frame). EM_TEST_FULL=1: the exhaustive unit sweep. EM_TEST_WORLD=1: the
world sweep only (every route snapshot with the camera-state, action-code
and AREA11-position variations), original tree against the native routine
with the ORIGINAL callees bound as its workers, whole RAM and scratchpad
compared.
"""
import ctypes as C
import hashlib
import math
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
from test_player_fall_reference import FallEE, nested_bits  # noqa: E402

MASK = 0xFFFFFFFF
LANE = os.environ.get('EM_LANE', 'camera_area11_specials_reference')
OUT = ROOT / 'build' / LANE

CAM, PLAYER, EYE, TGT = 0x8101E0, 0x8102B0, 0x8105D0, 0x8105E0
WINDOW, WINDOW_SIZE = 0x810000, 0x1000


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


# ======================================================================
# The routines and their callees
# ======================================================================

WALK, ROUTER, LOCKON, RELEASE, CLAMP = 0x195130, 0x193EB0, 0x1936E0, 0x197490, 0x191210
MAIN = {WALK: 0x1270, ROUTER: 0x390, LOCKON: 0x6AC, RELEASE: 0x2AC, CLAMP: 0xA0}
LEAVES = {0x11DF78: 0x1C, 0x102948: 0xC, 0x1031E0: 0x1C, 0x1029C0: 0x28, 0x1028D0: 0x14,
          0x102738: 0x24}
TRANSLATED = set(MAIN) | set(LEAVES)


def in_main(pc):
    return any(start <= pc < start + size for start, size in MAIN.items())


# Original callee -> (EmCamSpecialsWorkers field, argument kinds, result).
# Kinds: p = pointer (a0, a1, ... in order with i), i = int, f = float bits
# (f12, f13, ... in order). Result: 'v0' (int) or 'f0' (float bits).
SPEC = {
    0x1916C0: ('w_001916C0', 'ppi', None),
    0x1921D0: ('w_001921D0', 'ppi', None),
    0x193D90: ('w_00193D90', 'ppi', None),
    0x18D7B0: ('w_0018D7B0', 'pi', None),
    0x18C4B0: ('w_0018C4B0', 'pff', 'v0'),
    0x18C6A0: ('w_0018C6A0', 'ppf', 'v0'),
    0x191D40: ('w_00191D40', 'pff', None),
    0x192010: ('w_00192010', 'pfff', None),
    0x22FCA0: ('w_0022FCA0', 'ppi', None),
    0x1944B0: ('w_001944B0', 'ppi', 'v0'),
    0x194D10: ('w_00194D10', 'ppi', 'v0'),
    0x194DB0: ('w_00194DB0', 'ppi', None),
    0x230230: ('w_00230230', 'pp', 'v0'),
    0x823FE0: ('w_00823FE0', 'p', 'v0'),
    0x1AEDE0: ('w_001AEDE0', 'ii', None),
    0x191000: ('w_00191000', 'pp', None),
    0x1B0C60: ('w_001B0C60', 'iii', None),
    0x102C58: ('w_00102C58', 'ppp', None),
    0x1026A0: ('w_001026A0', 'ppp', None),
    0x11E748: ('w_0011E748', 'f', 'f0'),
    0x11E620: ('w_0011E620', 'ff', 'f0'),
    0x11E2A8: ('w_0011E2A8', 'f', 'f0'),
    0x11DE90: ('w_0011DE90', 'f', 'f0'),
    0x1B1470: ('w_001B1470', 'f', 'f0'),
    0x1B1240: ('w_001B1240', 'pff', 'f0'),
    0x193660: ('w_00193660', 'pp', 'v0'),
    0x197870: ('w_00197870', 'ppi', None),
    0x198440: ('w_00198440', 'ppi', None),
    0x1912B0: ('w_001912B0', 'p', None),
    0x19A910: ('w_0019A910', 'ppi', 'v0'),
    0x1B0300: ('w_001B0300', '', None),
}
FIELD_ADDRESS = {field: address for address, (field, _, _) in SPEC.items()}
# EmCamSpecialsWorkers, in header order.
WORKER_ORDER = ('w_001916C0', 'w_001921D0', 'w_00193D90', 'w_0018D7B0', 'w_0018C4B0',
                'w_0018C6A0', 'w_00191D40', 'w_00192010', 'w_0022FCA0', 'w_001944B0',
                'w_00194D10', 'w_00194DB0', 'w_00230230', 'w_00823FE0', 'w_001AEDE0',
                'w_00191000', 'w_001B0C60', 'w_00102C58', 'w_001026A0', 'w_0011E748',
                'w_0011E620', 'w_0011E2A8', 'w_0011DE90', 'w_001B1470', 'w_001B1240',
                'w_00193660', 'w_00197870', 'w_00198440', 'w_001912B0', 'w_0019A910',
                'w_001B0300')
assert sorted(WORKER_ORDER) == sorted(FIELD_ADDRESS)


def jal_targets(elf):
    ee = EE(elf)
    targets = set()
    for start, size in list(MAIN.items()) + list(LEAVES.items()):
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            if word >> 26 == 3:
                targets.add((word & 0x3FFFFFF) << 2)
    return targets


def check_callee_set(elf):
    """Every jal target of the translated routines is translated here or
    hooked, and every hook is a real target."""
    targets = jal_targets(elf)
    missing = sorted(t for t in targets if t not in SPEC and t not in TRANSLATED)
    assert not missing, ('callees neither hooked nor translated', [hex(t) for t in missing])
    unused = sorted(t for t in SPEC if t not in targets)
    assert not unused, ('hooked addresses no translated routine calls', [hex(t) for t in unused])
    return len(targets)


def branch_sites(elf):
    ee = EE(elf)
    sites = set()
    for start, size in MAIN.items():
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if op in (4, 20) and rs == 0 and rt == 0:
                continue                                  # b: unconditional
            if ee.branch(word, pc) is not None:
                sites.add(pc)
    return sites


# ======================================================================
# Native side (ctypes)
# ======================================================================

U32, I32, VP = C.c_uint32, C.c_int32, C.c_void_p
P = C.POINTER


class LiveActor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', VP), ('link_prev', VP),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


class Scratch(C.Structure):
    _fields_ = [('s3040', U32 * 4), ('s31B0', U32 * 4), ('s3400', U32 * 16), ('s3600', U32 * 4),
                ('s3630', U32 * 4), ('s38A0', U32 * 4), ('s3A20', U32), ('s3A24', U32),
                ('s3B50', U32 * 4), ('s3B80', C.c_uint16), ('s3B8D', C.c_uint8)]


# (scratchpad address, Scratch field, size)
SPAD_FIELDS = ((0x70003040, 's3040', 16), (0x700031B0, 's31B0', 16), (0x70003400, 's3400', 64),
               (0x70003600, 's3600', 16), (0x70003630, 's3630', 16), (0x700038A0, 's38A0', 16),
               (0x70003A20, 's3A20', 4), (0x70003A24, 's3A24', 4), (0x70003B50, 's3B50', 16),
               (0x70003B80, 's3B80', 2), (0x70003B8D, 's3B8D', 1))
WORLD_POINTERS = (('d8101E0', 0x8101E0), ('d8105D0', 0x8105D0), ('d8105E0', 0x8105E0),
                  ('d81069C', 0x81069C), ('d8106B8', 0x8106B8), ('d8106F2', 0x8106F2),
                  ('d810700', 0x810700), ('d810701', 0x810701), ('d810702', 0x810702),
                  ('d81078B', 0x81078B), ('d810803', 0x810803), ('d810E74', 0x810E74))


class World(C.Structure):
    _fields_ = [(name, VP) for name, _ in WORLD_POINTERS] + [('spad', P(Scratch))]


def fn_type(kinds, result):
    args = [VP]
    for kind in kinds:
        args.append({'p': VP, 'i': I32, 'f': U32}[kind])
    if result == 'v0':
        args.append(P(I32))
    elif result == 'f0':
        args.append(P(U32))
    return C.CFUNCTYPE(C.c_int, *args)


FN = {field: fn_type(SPEC[FIELD_ADDRESS[field]][1], SPEC[FIELD_ADDRESS[field]][2])
      for field in WORKER_ORDER}


class Workers(C.Structure):
    _fields_ = [('ctx', VP)] + [(field, FN[field]) for field in WORKER_ORDER]


class Specials(C.Structure):
    _fields_ = [('world', World), ('w', Workers), ('fault_address', U32)]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('camera_specials.dylib' if sys.platform == 'darwin' else 'camera_specials.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc',
                    'src/game/em_camera_area11_specials.c', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    S, A = P(Specials), P(LiveActor)
    native.em_cam_specials_00195130.argtypes = [S, VP, A]
    native.em_cam_specials_00193EB0.argtypes = [S, VP, A, C.c_int32]
    native.em_cam_specials_001936E0.argtypes = [S, VP, A]
    native.em_cam_specials_00197490.argtypes = [S, VP, A, C.c_int32]
    native.em_cam_specials_00191210.argtypes = [S]
    return native


class Mirror:
    """The native side's storage for the compared state: the 0x810000 window
    (a byte buffer), the player record (EmPlayerLiveActor) and the scratch
    struct, addressable by original address."""

    def __init__(self, window, spad):
        self.ram = (C.c_uint8 * WINDOW_SIZE).from_buffer_copy(window)
        self.live = LiveActor()
        C.memmove(self.live.bytes, window[PLAYER - WINDOW:PLAYER - WINDOW + 0x320], 0x320)
        self.scratch = Scratch()
        base = C.addressof(self.scratch)
        self.regions = [(PLAYER, 0x320, C.addressof(self.live))]
        for address, name, size in SPAD_FIELDS:
            at = base + getattr(Scratch, name).offset
            C.memmove(at, spad[address], size)
            self.regions.append((address, size, at))
        self.regions.append((WINDOW, WINDOW_SIZE, C.addressof(self.ram)))

    def host(self, address, size=1):
        for start, length, at in self.regions:
            if start <= address and address + size <= start + length:
                return at + address - start
        raise AssertionError(('address outside the mirrored state', hex(address), size))

    def ee_address(self, pointer):
        if pointer is None:
            return None
        for start, length, at in self.regions:
            if at <= pointer < at + length:
                return start + pointer - at
        return None

    def load(self, address, size):
        return C.string_at(self.host(address, size), size)

    def save(self, address, value, size):
        C.memmove(self.host(address, size), (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little'), size)

    def state(self):
        window = bytearray(bytes(self.ram))
        window[PLAYER - WINDOW:PLAYER - WINDOW + 0x320] = bytes(self.live.bytes)
        return bytes(window) + b''.join(self.load(a, n) for a, _, n in SPAD_FIELDS)

    def world(self):
        w = World(**{name: self.host(address) for name, address in WORLD_POINTERS})
        w.spad = C.pointer(self.scratch)
        return w


def ee_state(ee):
    return ee.read(WINDOW, WINDOW_SIZE) + b''.join(ee.read(a, n) for a, _, n in SPAD_FIELDS)


def in_state(address):
    if WINDOW <= address < WINDOW + WINDOW_SIZE:
        return True
    return any(a <= address < a + n for a, _, n in SPAD_FIELDS)


def digest(state):
    return hashlib.sha1(state).hexdigest()[:16]


def call_native(native, specials, entry, mirror, extra):
    S = C.byref(specials)
    cam = mirror.host(CAM)
    A = C.byref(mirror.live)
    if entry == WALK: return native.em_cam_specials_00195130(S, cam, A)
    if entry == ROUTER: return native.em_cam_specials_00193EB0(S, cam, A, extra)
    if entry == LOCKON: return native.em_cam_specials_001936E0(S, cam, A)
    if entry == RELEASE: return native.em_cam_specials_00197490(S, cam, A, extra)
    return native.em_cam_specials_00191210(S)


def entry_args(entry, extra):
    if entry in (ROUTER, RELEASE):
        return (CAM, PLAYER, extra)
    if entry == CLAMP:
        return ()
    return (CAM, PLAYER)


# ======================================================================
# Unit cases
# ======================================================================

def near(bits_value, rng):
    return (bits_value + rng.choice((-1, 0, 0, 1))) & MASK


# Threshold bit patterns the routines compare against (from the translated
# code; each is also sampled one ulp either side).
X_EDGES = (0x43114CCD, 0x43B38000, 0x43C56666, 0x445A0000, 0x4425C000)
Y_EDGES = (0x43390000, 0x42580000, 0x43660000, 0x4359199A, 0x43B20000, 0x43B68000, 0x431F0000)
Z_EDGES = (0x435C0000, 0x43B40000, 0x430E0000, 0x43160000, 0x43280000, 0x43230000, 0x44610000,
           0x4438A000)
# (centre x, centre z, radius) of the distance tests, as floats.
CENTRES = ((321.5, 216.7, 8.0), (892.1, 929.5, 8.0), (340.0, 270.0, 205.0), (340.0, 270.0, 140.0))
B_CENTRES = ((129.7, 150.5, 5.0), (129.8, 160.5, 5.0))
AREAS = (0, 4, 6, 8, 8, 8, 8, 0xB, 0xB, 0xB, 0xB, 0xD, 0xD, 0xE, 0xF, 0x10, 0x11, 0x13, 0x13, 0x13,
         0x16, 3)
AREA_CODES = {0: (0x14, 0x15), 0xF: (0x14, 0x15), 0x13: (0x14, 0x15, 6, 7, 8, 9, 0x2C, 0x2D),
              8: (6, 7, 8, 9, 0xA), 0xB: (6, 7, 8, 9), 0xD: (6, 7, 8, 9, 0x2C), 0x16: (8, 0x2D)}
CODES = (0, 1, 1, 2, 4, 5, 6, 7, 8, 9, 0xA, 0xC, 0xD, 0xF, 0x10, 0x12, 0x14, 0x15, 0x17, 0x18,
         0x21, 0x28, 0x29, 0x2A, 0x2C, 0x2D, 0x30, 0x7FFFFFFF, 0xFFFFFFFF)
# Per-area thresholds and distance centres (the arms that read them).
AREA_GEOMETRY = {
    0xB: {'x': (0x43B38000, 0x43C56666), 'y': (0x43390000,), 'z': (0x435C0000,),
          'centres': ((321.5, 216.7, 8.0),)},
    8: {'x': (0x43114CCD,), 'y': (0x4359199A, 0x43660000), 'z': (0x430E0000, 0x43160000, 0x43280000,
                                                               0x43230000)},
    0x13: {'x': (0x445A0000,), 'y': (0x43B20000, 0x43B68000), 'z': (0x44610000,),
           'centres': ((892.1, 929.5, 8.0),)},
    0xD: {'x': (0x4425C000,), 'y': (0x431F0000,), 'z': (0x4438A000,)},
    4: {'y': (0x42580000,), 'z': (0x43B40000,)},
    0x11: {'centres': ((340.0, 270.0, 205.0), (340.0, 270.0, 140.0))},
}
ENTRIES = (WALK,) * 10 + (ROUTER,) * 3 + (LOCKON,) * 5 + (RELEASE,) * 4 + (CLAMP,)


def make_case(seed):
    rng = random.Random(seed)
    entry = rng.choice(ENTRIES)
    window = bytearray(rng.getrandbits(8) for _ in range(WINDOW_SIZE))
    wild = rng.random() < 0.05

    def put(address, size, value):
        window[address - WINDOW:address - WINDOW + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')

    def putf(address, value):
        put(address, 4, value if isinstance(value, int) else F(value))

    choose = rng.choice
    cam = lambda off, size, value: put(CAM + off, size, value)
    camf = lambda off, value: putf(CAM + off, value)
    pl = lambda off, size, value: put(PLAYER + off, size, value)
    plf = lambda off, value: putf(PLAYER + off, value)
    # the camera block
    states = {WALK: (0, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 4, 4, 5), LOCKON: (0, 0, 1, 1, 1, 2)}.get(entry, (0, 1, 2))
    cam(1, 1, choose(states))
    cam(2, 1, choose((0, 1, 2, 3, 4, 5)))
    cam(5, 1, choose((0, 0, 0, 1)))
    cam(6, 1, choose((0, 0, 3, 3, 7, 1)))
    cam(8, 2, choose((0x78, 1, 2, 0, 0x50, 0x51, 0x47, 0x46, 0xFFFF, 0x8000, 0x7FFF, 0x4C)))
    cam(0x6D, 1, choose((0, 1, 0x80)))
    cam(0x8B, 1, choose((0, 0, 1)))
    if not wild:
        for off in (0x10, 0x18, 0x20, 0x28):
            camf(off, rng.uniform(-400, 1200))
        if rng.random() < 0.3:
            camf(0x10, choose((0x4303199A, 0x4303199B, 0x4327CCCD)))
        camf(0x14, choose((rng.uniform(0, 400), 282.4, 146.2, 223.6, 240.7, 0x438D3333, 0x438D3334,
                           0x43123333, 0x43123332, 0x435F999A, 0x435F999B)))
        camf(0x24, rng.uniform(0, 400))
        for off in (0x30, 0x34, 0x38, 0x3C, 0x44, 0x48):
            camf(off, rng.uniform(-3.2, 3.2))
        camf(0xC, choose((rng.uniform(-40, 40), 0.0, -0.0, 20.0, -20.0)))
        camf(0x4C, choose((rng.uniform(0, 30), 7.0, -7.0)))
        camf(0x50, choose((rng.uniform(-50, 250), 0.0)))
        camf(0x54, choose((rng.uniform(150, 450), 1000.0)))
        camf(0x5C, choose((rng.uniform(-20, 40), 0.0, 10.0)))
        camf(0x64, choose((0xC23B3333, 0xC23B3332, rng.uniform(-60, 60))))
        camf(0x8C, choose((rng.uniform(-10, 20), 0.0, 5.0)))
        camf(0x94, rng.uniform(-60, 60))
        camf(0x98, rng.uniform(-30, 30))
        # the eye / target and the globals
        for i in range(8):
            putf(EYE + 4 * i, rng.uniform(-400, 1200))
        putf(EYE + 8, choose((0x43FD8000, 0x43FD7FFF, 0x43FD8001, F(rng.uniform(0, 1000)))))
        camf(0x18, choose((0x43FD8000, 0x43FD7FFF, 0x43FD8001, F(rng.uniform(0, 1000)))))
        putf(0x81069C, choose((rng.uniform(-60, 60), -20.0, -10.0, -30.0, 0.0, 7.0, -7.0, 40.0)))
    area = choose(AREAS)
    put(0x810700, 1, area)
    put(0x810701, 1, 3 if area == 8 and rng.random() < 0.6 else choose((0, 0, 1, 2, 3, 3)))
    put(0x810702, 1, choose((0, 0, 4, 5, 6, 7, 8, 9, 3)))
    put(0x8106B8, 1, choose((0, 0, 1)))
    put(0x8106F2, 1, choose((0, 1, 2, 5, 9)))
    put(0x81078B, 1, choose((0, 0xFF, 1)))
    put(0x810803, 1, choose((3, 3, 0)))
    put(0x810E74, 2, choose((0x400, 0x800, 0, 0xFFFF)))
    # the player record
    pl(0x230, 4, choose(AREA_CODES.get(area, CODES)) if rng.random() < 0.5 else choose(CODES))
    pl(0xF, 1, choose((0xB, 5, 0)))
    focus_area8 = area == 8 and window[0x701] == 3 and rng.random() < 0.6
    if focus_area8:
        pl(0x230, 4, choose((6, 7, 8, 9, 0xA, 0xA)))
        camf(0x14, choose((near(choose((0x4370B333, 0x438D3333, 0x43123333, 0x435F999A)), rng),
                           F(rng.uniform(100, 300)))))
    if not wild:
        geometry = AREA_GEOMETRY.get(area, {}) if rng.random() < 0.8 else {}
        axes = []
        for key, edges_all, low, high in (('x', X_EDGES, -100, 1000), ('y', Y_EDGES, 0, 500),
                                         ('z', Z_EDGES, -100, 1300)):
            edges = geometry.get(key) or edges_all
            roll = rng.random()
            if roll < 0.35:
                axes.append(near(choose(edges), rng))
            elif roll < 0.7:
                axes.append(F(struct.unpack('<f', struct.pack('<I', choose(edges)))[0] + rng.uniform(-40, 40)))
            else:
                axes.append(F(rng.uniform(low, high)))
        xb, yb, zb = axes
        centres = geometry.get('centres', CENTRES)
        if rng.random() < 0.35:
            cx, cz, r = choose(centres)
            angle, d = rng.uniform(0, 6.2831853), rng.uniform(0, r * 1.2)
            xb, zb = F(cx + d * math.cos(angle)), F(cz + d * math.sin(angle))
        if rng.random() < (0.3 if area == 0xB else 0.05):          # the AREA11 box
            xb, yb, zb = F(rng.uniform(358, 396)), F(rng.uniform(180, 190)), F(rng.uniform(215, 225))
        if focus_area8:               # area 8, room 3, code 0xA: the z band and the height
            yb = F(rng.choice((rng.uniform(200, 217.1), rng.uniform(217.1, 260))))
            zb = F(rng.uniform(135, 175))
        plf(0xA0, xb); plf(0xA4, yb); plf(0xA8, zb)
        if rng.random() < 0.3 and not focus_area8:     # the camera height near the lock-on band
            y = struct.unpack('<f', struct.pack('<I', yb))[0]
            camf(0x14, choose((y + 29.5, y + rng.uniform(25, 35), y + rng.uniform(5, 40))))
        if rng.random() < (0.8 if focus_area8 else 0.5):
            cx, cz, r = choose(B_CENTRES[:1] * 3 + B_CENTRES[1:]) if focus_area8 else choose(B_CENTRES)
            plf(0xB0, cx + rng.uniform(-r, r) * 1.2); plf(0xB8, cz + rng.uniform(-r, r) * 1.2)
        else:
            plf(0xB0, rng.uniform(-100, 1000)); plf(0xB8, rng.uniform(-100, 1000))
        plf(0xB4, rng.uniform(0, 400))
        plf(0x38, choose((0.0, -0.0, 0.1, -0.1, 3.0)))
        plf(0xC4, rng.uniform(-3.2, 3.2))
    spad = {}
    for address, name, size in SPAD_FIELDS:
        spad[address] = bytes(rng.getrandbits(8) for _ in range(size))
    spad[0x70003B8D] = bytes([choose((0, 0, 1))])
    spad[0x70003B80] = choose((0, 0x400, 0x800, 0xFFFF)).to_bytes(2, 'little')
    extra = choose((0, 1, 2, 2, 3)) if entry == ROUTER else choose((0, 1))
    return {'seed': seed, 'entry': entry, 'window': bytes(window), 'spad': spad, 'extra': extra}


V0_KINDS = {'w_0018C4B0': (0, 1, 2, 3, 4, 5, 6, 7), 'w_0018C6A0': (0, 1, 2, 3, 4, 5, 6, 7)}
F0_POOLS = {'w_0011E748': (F(7.0), F(7.0) - 1, F(7.0) + 1, F(8.0), F(8.0) - 1, F(0.25), F(0.25) - 1,
                           F(0.25) + 1)}


def effect_for(rng, field):
    """What a hooked callee does: its result and the writes it makes. A write
    is (base, offset, size, value) with base 'a0'/'a1' (relative to that
    pointer argument) or an absolute address."""
    e = {'v0': 0, 'f0': 0, 'writes': []}
    w = e['writes']
    chance = rng.random
    e['v0'] = rng.choice(V0_KINDS.get(field, (0, 0, 1, 2)))
    pool = F0_POOLS.get(field)
    if pool and chance() < 0.6:
        e['f0'] = rng.choice(pool)
    elif field == 'w_0011E748':
        e['f0'] = F(rng.uniform(0, 20))
    else:
        e['f0'] = F(rng.uniform(-3.2, 3.2))
    if field == 'w_001026A0':
        for i in range(4): w.append(('a0', 4 * i, 4, F(rng.uniform(-400, 1200))))
        if chance() < 0.4:             # a height inside the lock-on band
            w.append(('a0', 4, 4, ('plus', PLAYER + 0xA4, rng.choice((28.5, 29.0, 29.5, 30.0, 30.5)))))
    elif field == 'w_00102C58':
        for i in range(16): w.append(('a0', 4 * i, 4, F(rng.uniform(-2, 2))))
    elif field == 'w_0018C4B0' and chance() < 0.7:
        w.append(('a0', 4, 4, F(rng.uniform(0, 400))))
    elif field == 'w_0018C6A0' and chance() < 0.7:
        w.append(('a1', 0, 4, F(rng.uniform(-400, 1200))))
        w.append(('a1', 8, 4, F(rng.uniform(-400, 1200))))
    elif field == 'w_0019A910' and chance() < 0.8:
        for i in range(4): w.append((0x700031B0, 4 * i, 4, F(rng.uniform(-400, 1200))))
    # side effects a real callee may have on the state the routine reads next
    if chance() < 0.12:
        w.append((CAM, rng.choice((1, 2, 5, 6)), 1, rng.choice((0, 1, 2, 3, 4))))
    if chance() < 0.08:
        w.append((0x810700, 0, 1, rng.choice(AREAS)))
    if chance() < 0.08:
        w.append((PLAYER, 0x230, 4, rng.choice(CODES)))
    if chance() < 0.08:
        w.append((CAM, 0x14, 4, F(rng.uniform(0, 400))))
    if chance() < 0.05:
        w.append((EYE, 4 * rng.randrange(8), 4, F(rng.uniform(-400, 1200))))
    return e


class Script:
    def __init__(self, seed, fail_at=None):
        self.seed, self.count, self.fail_at = seed, 0, fail_at

    def next(self, field):
        rng = random.Random('%d:%d:%s' % (self.seed, self.count, field))
        index = self.count
        self.count += 1
        return index, effect_for(rng, field)


def value_of(value, load):
    """A scripted value: raw bits, or ('plus', address, delta) = the float at
    address (as the side sees it when the callee returns) plus delta."""
    if isinstance(value, tuple):
        _, address, delta = value
        return F(struct.unpack('<f', struct.pack('<I', load(address)))[0] + delta)
    return value


def resolve(base, args, kinds):
    if isinstance(base, int):
        return base
    n = int(base[1])
    assert kinds[n] == 'p', base
    return args[n]


def oracle_args(ee, kinds):
    out, ai, fi = [], 0, 0
    for kind in kinds:
        if kind == 'f':
            out.append(ee.f[12 + fi] & MASK); fi += 1
        else:
            value = ee.r[4 + ai] & MASK
            out.append(value if kind == 'p' else s32(value)); ai += 1
    return tuple(out)


class CoverEE(FallEE):
    def __init__(self, elf):
        super().__init__(elf)
        self.outcomes = set()

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and in_main(pc):
            self.outcomes.add((pc, b[0]))
        return b


class UnitOracle:
    """The original routines on a CamEE with every callee hooked."""

    def __init__(self, elf):
        self.ee = CoverEE(elf)
        for address in SPEC:
            self.ee.hooks[address] = self.hook(address)
        self.written = set()
        save = self.ee.save

        def guarded(address, value, size=4):
            address &= MASK
            if not 0x7F000000 <= address < 0x7F100000:
                for i in range(size):
                    self.written.add(address + i)
            save(address, value, size)
        self.ee.save = guarded

    def hook(self, address):
        field, kinds, result = SPEC[address]

        def run(ee):
            args = oracle_args(ee, kinds)
            for kind, value in zip(kinds, args):
                if kind == 'p':
                    assert in_state(value), (field, 'pointer argument outside the compared state', hex(value))
            self.log.append((field, args, digest(ee_state(ee))))
            _, e = self.script.next(field)
            for base, off, size, value in e['writes']:
                ee.save(resolve(base, args, kinds) + off, value_of(value, ee.load), size)
            if result == 'v0':
                ee.ret_int(e['v0'])
            elif result == 'f0':
                ee.f[0] = e['f0']
        return run

    def run(self, case, script):
        ee = self.ee
        self.script, self.log = script, []
        ee.r, ee.rh = [0] * 32, [0] * 32
        ee.f, ee.acc, ee.cond = [0] * 32, 0, False
        ee.vacc, ee.q = [0, 0, 0, 0], 0
        ee.vf = [[0, 0, 0, 0] for _ in range(32)]
        ee.vf[0][3] = F(1.0)
        ee.r[28], ee.r[29] = 0x27D370, shared.STACK_TOP
        ee.write(WINDOW, case['window'])
        for address, _, size in SPAD_FIELDS:
            ee.write(address, case['spad'][address])
        self.written.clear()
        ee.call(case['entry'], entry_args(case['entry'], case['extra']))
        outside = sorted(a for a in self.written if not in_state(a))
        assert not outside, (case['seed'], 'original stores outside the compared state',
                             [hex(a) for a in outside[:16]])
        return {'state': ee_state(ee), 'log': self.log}


class NativeRun:
    """em_camera_area11_specials.c with Python workers replaying the script."""

    def __init__(self, native, case, script, missing=None):
        self.native, self.case, self.script, self.log = native, case, script, []
        self.mirror = Mirror(case['window'], case['spad'])
        self.error = None
        self.keep = []
        fields = {}
        for field in WORKER_ORDER:
            fields[field] = FN[field](self.worker(field)) if field != missing else FN[field]()
            self.keep.append(fields[field])
        self.specials = Specials(self.mirror.world(), Workers(None, **fields), 0)

    def worker(self, field):
        kinds, result = SPEC[FIELD_ADDRESS[field]][1:]
        mirror = self.mirror

        def fn(_ctx, *raw):
            if self.error is not None:
                return -1
            try:
                args = []
                for kind, value in zip(kinds, raw):
                    if kind == 'p':
                        address = mirror.ee_address(value)
                        assert address is not None, (field, 'pointer outside the mirrored state')
                        args.append(address)
                    elif kind == 'i':
                        args.append(int(value))
                    else:
                        args.append(value & MASK)
                args = tuple(args)
                self.log.append((field, args, digest(mirror.state())))
                index, e = self.script.next(field)
                if self.script.fail_at == index:
                    return -1
                for base, off, size, value in e['writes']:
                    load = lambda at: int.from_bytes(mirror.load(at, 4), 'little')
                    mirror.save(resolve(base, args, kinds) + off, value_of(value, load), size)
                if result == 'v0':
                    raw[len(kinds)][0] = s32(e['v0'])
                elif result == 'f0':
                    raw[len(kinds)][0] = e['f0']
                return 0
            except BaseException as error:
                self.error = error
                return -1
        return fn

    def run(self):
        result = call_native(self.native, self.specials, self.case['entry'], self.mirror,
                             self.case['extra'])
        if self.error is not None:
            raise self.error
        return result, {'state': self.mirror.state(), 'log': self.log,
                        'fault': self.specials.fault_address}


ELF = NATIVE = ORACLE = None


def state_diff(a, b):
    out = []
    names = [(WINDOW, WINDOW_SIZE, 'ram')] + [(addr, n, name) for addr, name, n in SPAD_FIELDS]
    at = 0
    for base, size, name in names:
        for k in range(size):
            if a[at + k] != b[at + k]:
                out.append(hex(base + k))
        at += size
    return out[:24]


def run_case(seed):
    global ORACLE
    if ORACLE is None:
        ORACLE = UnitOracle(ELF)
    case = make_case(seed)
    want = ORACLE.run(case, Script(seed))
    result, got = NativeRun(NATIVE, case, Script(seed)).run()
    where = (seed, hex(case['entry']), case['window'][CAM - WINDOW + 1], case['window'][0x700])
    assert result == 0 and got['fault'] == 0, (where, 'native fault', result, hex(got['fault']))
    assert want['log'] == got['log'], (where, 'worker calls', want['log'], got['log'])
    assert want['state'] == got['state'], (where, 'state differs at', state_diff(want['state'], got['state']))
    faults = 0
    if want['log'] and seed % 5 == 0:
        k = random.Random(seed).randrange(len(want['log']))
        result, cut = NativeRun(NATIVE, case, Script(seed, fail_at=k)).run()
        assert result == -1, (where, 'fault not reported', k)
        assert cut['log'] == want['log'][:k + 1], (where, 'calls after a fault', k)
        assert cut['fault'] == FIELD_ADDRESS[want['log'][k][0]], (where, 'fault address', hex(cut['fault']))
        faults = 1
    refusals = 0
    if seed % 7 == 0:
        refusals = missing_checks(case, want)
    return (case['entry'], len(want['log']), faults, refusals, tuple(sorted(ORACLE.ee.outcomes)))


def missing_checks(case, want):
    """Each worker missing in turn: the routine either refuses (-1, the
    missing address recorded, and - when it refused before any call - no
    write at all) or runs identically without ever needing that worker."""
    called = {entry[0] for entry in want['log']}
    count = 0
    for field in WORKER_ORDER:
        result, got = NativeRun(NATIVE, case, Script(case['seed']), missing=field).run()
        if result == -1:
            assert got['fault'] == FIELD_ADDRESS[field], (case['seed'], field, hex(got['fault']))
            assert got['log'] == want['log'][:len(got['log'])], (case['seed'], field, 'calls before refusing')
            if not got['log']:
                start = NativeRun(NATIVE, case, Script(case['seed']))
                assert got['state'] == start.mirror.state(), (case['seed'], field, 'wrote before refusing')
            count += 1
        else:
            assert result == 0 and field not in called, (case['seed'], field, 'needed worker missing', result)
            assert got['log'] == want['log'] and got['state'] == want['state'], (case['seed'], field)
    return count


def world_refusal(native):
    """A world pointer missing: every entry point refuses before any call or write."""
    case = make_case(3)
    count = 0
    for name, _ in WORLD_POINTERS + (('spad', 0),):
        for entry in MAIN:
            case = dict(case, entry=entry)
            run = NativeRun(native, case, Script(3))
            setattr(run.specials.world, name, None)
            before = run.mirror.state()
            result, got = run.run()
            assert result == -1 and got['log'] == [] and got['state'] == before, (name, hex(entry))
            count += 1
    return count


def main():
    global ELF, NATIVE
    started = time.time()
    ELF = read_elf()
    callees = check_callee_set(ELF)
    NATIVE = build_native()
    total = reference_mode.pick(60000, 60000)
    seeds = reference_mode.select(range(total), 12000, 0xCA11)
    results = reference_mode.parallel_map(run_case, seeds)
    outcomes, entries = set(), {}
    faults = refusals = calls = 0
    for entry, count, fault, refused, cover in results:
        outcomes.update(cover)
        entries[entry] = entries.get(entry, 0) + 1
        faults += fault
        refusals += refused
        calls += count
    sites = branch_sites(ELF)
    missing = sorted('%06X %s' % (pc, 'taken' if taken else 'not taken')
                     for pc in sites for taken in (True, False) if (pc, taken) not in outcomes)
    assert not missing, ('branch outcomes never exercised', missing)
    assert set(entries) == set(MAIN), ('entry points never run', entries)
    world_refusals = world_refusal(NATIVE)
    reference_mode.banner(reference_mode.part(len(seeds), total, 'cases'),
                          '%d jal targets (all hooked or translated)' % callees)
    print('camera specials vs original instructions: PASS %d cases (%s), %d worker calls identical '
          '(args + whole compared state at each call), every one of %d conditional branches both '
          'ways, %d fault-stop cuts, %d missing-worker refusals, %d missing-world refusals (%.1fs)' % (
              len(seeds), ', '.join('%06X %d' % kv for kv in sorted(entries.items())), calls,
              len(sites), faults, refusals, world_refusals, time.time() - started))
    world_main(quick=True)


# ======================================================================
# World mode: the route snapshots, original callees bound as workers
# ======================================================================

def route_snapshots():
    """(name, EE RAM, scratchpad) of every captured AREA11 route beat."""
    out = []
    for beat in sorted(p.name for p in shared.ROUTE.iterdir() if p.is_dir()):
        files = [shared.ROUTE / beat / n for n in ('eeMemory.bin', 'scratchpad.bin')]
        if all(f.exists() for f in files):
            out.append((beat, files[0], files[1]))
    return out


class WorldWorkers:
    """Every EmCamSpecialsWorkers entry bound to its ORIGINAL routine,
    executed in the same EE on the same world: the mirrored state is written
    into the EE before each call and read back after it."""

    def __init__(self, ee, mirror):
        self.ee, self.mirror, self.error, self.keep = ee, mirror, None, []
        self.counts = {}
        fields = {}
        for field in WORKER_ORDER:
            fields[field] = FN[field](self.bind(field))
            self.keep.append(fields[field])
        self.workers = Workers(None, **fields)

    def sync_in(self):
        ee, m = self.ee, self.mirror
        state = m.state()
        ee.write(WINDOW, state[:WINDOW_SIZE])
        at = WINDOW_SIZE
        for address, _, size in SPAD_FIELDS:
            ee.write(address, state[at:at + size]); at += size

    def sync_out(self):
        ee, m = self.ee, self.mirror
        window = ee.read(WINDOW, WINDOW_SIZE)
        C.memmove(m.ram, window, WINDOW_SIZE)
        C.memmove(m.live.bytes, window[PLAYER - WINDOW:PLAYER - WINDOW + 0x320], 0x320)
        for address, _, size in SPAD_FIELDS:
            C.memmove(m.host(address, size), ee.read(address, size), size)

    def bind(self, field):
        address = FIELD_ADDRESS[field]
        kinds, result = SPEC[address][1:]

        def fn(_ctx, *raw):
            if self.error is not None:
                return -1
            try:
                ints, floats = [], []
                for kind, value in zip(kinds, raw):
                    if kind == 'p':
                        at = self.mirror.ee_address(value)
                        assert at is not None, (field, 'pointer outside the mirrored state')
                        ints.append(at)
                    elif kind == 'i':
                        ints.append(int(value) & MASK)
                    else:
                        floats.append(value & MASK)
                self.sync_in()
                v0, f0 = nested_bits(self.ee, address, ints, floats)
                self.sync_out()
                if result == 'v0':
                    raw[len(kinds)][0] = s32(v0)
                elif result == 'f0':
                    raw[len(kinds)][0] = f0 & MASK
                self.counts[field] = self.counts.get(field, 0) + 1
                return 0
            except BaseException as error:
                self.error = error
                return -1
        return fn


WORLD = {}
WORLD_CODES = (1, 6, 7, 8, 9, 0xA, 0xC, 0xD, 0x10, 0x12, 0x21, 0x28, 0x29, 0x2A, 0x2C, 0x3)


def world_variants(quick):
    """(entry, camera state, code or None, position or None, extra, poke)
    per snapshot; poke names an extra setting world_case applies."""
    out = [(WALK, None, None, None, 0, None)]
    if quick:
        return out
    for code in WORLD_CODES:
        for state in (0, 1):
            out.append((WALK, state, code, None, 0, None))
    for position in ((370.0, 184.0, 215.0), (359.5, 184.9, 219.9), (394.0, 150.0, 100.0),
                     (321.5, 216.7, 216.7), (325.0, 216.0, 212.0), (321.0, 230.0, 222.0)):
        for code in (1, 6, 7):
            out.append((WALK, 1, code, position, 0, None))
    for state in (2, 3):
        out.append((WALK, state, None, None, 0, None))
    for handled in (0, 2):
        for code in (1, 0x21, 8, 0x12):
            out.append((ROUTER, None, code, None, handled, None))
    for state in (0, 1):
        for code in (1, 2, 0xF, 4, 0xC, 0xD, 0x21, 8):
            out.append((LOCKON, state, code, None, 0, None))
    for code in (1, 8):                 # the eye within 8 of the player: the swing-out
        out.append((LOCKON, 1, code, None, 0, 'eye_close'))
    for a2 in (0, 1):
        for code in (1, 0x17, 0x29, 0xC, 0xD, 8):
            out.append((RELEASE, None, code, None, a2, None))
            out.append((RELEASE, None, code, None, a2, 'cam5'))
    return out


def world_case(job):
    beat, variant = job
    entry, state, code, position, extra, poke = variant
    ram, spad = WORLD[beat]
    elf = WORLD['elf']

    def prepare(ee):
        if state is not None:
            ee.save(CAM + 1, state, 1)
        if code is not None:
            ee.save(PLAYER + 0x230, code)
        if position is not None:
            for i, value in enumerate(position):
                ee.save(PLAYER + 0xA0 + 4 * i, F(value))
        if poke == 'cam5':
            ee.save(CAM + 5, 1, 1)
        elif poke == 'eye_close':
            px, py, pz = (ee.load(PLAYER + 0xA0 + 4 * i) for i in range(3))
            fx, fz = (struct.unpack('<f', struct.pack('<I', v))[0] for v in (px, pz))
            for base in (EYE, CAM + 0x10):
                ee.save(base, F(fx + 3.0)); ee.save(base + 4, py); ee.save(base + 8, F(fz + 3.0))
        ee.r[28], ee.r[29] = 0x27D370, shared.STACK_TOP

    original = FallEE(elf, ram, spad)
    prepare(original)
    original.call(entry, entry_args(entry, extra))
    native_ee = FallEE(elf, ram, spad)
    prepare(native_ee)
    window = native_ee.read(WINDOW, WINDOW_SIZE)
    spad_fields = {a: native_ee.read(a, n) for a, _, n in SPAD_FIELDS}
    mirror = Mirror(window, spad_fields)
    world = WorldWorkers(native_ee, mirror)
    specials = Specials(mirror.world(), world.workers, 0)
    result = call_native(WORLD['native'], specials, entry, mirror, extra)
    if world.error is not None:
        raise world.error
    assert result == 0 and specials.fault_address == 0, (beat, variant, result, hex(specials.fault_address))
    world.sync_in()
    same_ram = original.mem == native_ee.mem
    same_spad = original.spad == native_ee.spad
    if not (same_ram and same_spad):
        where = [hex(k) for k in range(len(original.mem)) if original.mem[k] != native_ee.mem[k]][:16]
        spad_where = [hex(0x70000000 + k) for k in range(0x4000) if original.spad[k] != native_ee.spad[k]][:16]
        raise AssertionError((beat, variant, 'RAM differs at', where, 'scratchpad differs at', spad_where))
    return (beat, entry, sorted(world.counts.items()))


def world_main(quick=False):
    elf = read_elf()
    snapshots = route_snapshots()
    reference = shared.REFERENCE / 'playable_ee.bin'
    if not snapshots:
        raise SystemExit('world mode: no route snapshots under %s (docs/CAMERA_AREA11_SPECIALS.md)'
                         % shared.ROUTE)
    for beat, ram_path, spad_path in snapshots:
        WORLD[beat] = (ram_path.read_bytes(), spad_path.read_bytes())
    WORLD['elf'] = elf
    if 'native' not in WORLD:
        WORLD['native'] = NATIVE or build_native()
    started = time.time()
    beats = [b for b, _, _ in snapshots]
    jobs = [(beat, variant) for beat in beats for variant in world_variants(quick)]
    results = reference_mode.parallel_map(world_case, jobs)
    callees = {}
    per_entry = {}
    for _, entry, counts in results:
        per_entry[entry] = per_entry.get(entry, 0) + 1
        for field, n in counts:
            callees[field] = callees.get(field, 0) + n
    print('camera specials world (%s, %d route snapshots%s): PASS %d cases (%s), whole RAM + '
          'scratchpad identical; original workers called: %s (%.1fs)' % (
              'as-captured frame' if quick else 'variant sweep', len(beats),
              '' if reference.exists() else '', len(results),
              ', '.join('%06X %d' % kv for kv in sorted(per_entry.items())),
              ', '.join('%s %d' % (f[2:], n) for f, n in sorted(callees.items())),
              time.time() - started))


if __name__ == '__main__':
    if os.environ.get('EM_TEST_WORLD', '') not in ('', '0'):
        world_main()
    else:
        main()
