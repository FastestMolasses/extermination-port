#!/usr/bin/env python3
"""Execute the original camera routines and compare em_camera_leftovers*.c.

docs/CAMERA_LEFTOVERS.md. The user's pinned ELF (and, for the captured and
world modes, the captured RAM) supplies every instruction and table; none
are embedded here. Routines executed unmodified and compared:

  0018B9C0 the camera frame        0018BC20 the action dispatch
  00190F20 the area trigger        0018C0C0 the target copy
  001914A0 / 00191580 / 0018C5A0   camera action 8
  001916C0 the target placement    00191000 the orient-behind request
  0022FCA0 the boom / orbit        00230000 the tether, 00194D10 its region test
  0018DD20 the desired-eye solver  0018CE60 / 0018D910 the bounds
  0015CBA0 the state -> code map
plus the leaves they reach (0018C6A0, 0018C4B0, 00191D40, 00192010,
00191390 of em_camera_follow_original.c, 0011DF78 and the SDK vector
leaves). Every other callee is hooked, scripted per case and recorded (never
simulated as a claim about the callee); the native module gets the same
script through its workers. The test asserts that the hooked set is exactly
the set of jal targets of these routines.

Arithmetic: LeftEE is the follow test's CameraEE (the fall test's FallEE:
every COP1 op and VU0 macro op through tools/ee_float_model.py).

Default run (~10 s): unit cases with every conditional branch of the
translated routines taken both ways, fail-stop cuts, missing-worker and
missing-global refusals, 0015CBA0 over its whole input domain, and the
captured check: the original camera frame 0018B9C0 over each captured RAM
image (startup-reference state 04 and every route beat's source) with these
translations in place of the originals (workers bound to the original
callees in the same EE), whole RAM and scratchpad compared with the
all-original run. EM_TEST_FULL=1: the exhaustive sweep. EM_TEST_WORLD=1: the
route beats replayed (the player stage as test_player_slide_reference drives
it, then the camera frame every frame), original against translated, whole
RAM and scratchpad every frame.
"""
import ctypes as C
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
from test_player_fall_reference import nested_bits  # noqa: E402
import test_camera_follow_original_reference as follow  # noqa: E402

MASK = 0xFFFFFFFF
LANE = os.environ.get('EM_LANE', 'b7-camera-leftovers')
OUT = ROOT / 'build' / LANE


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def number(word):
    return struct.unpack('<f', struct.pack('<I', word & MASK))[0]


# ======================================================================
# The routines and their callees
# ======================================================================

MINE = {
    0x18B9C0: ('frame', 0x25C), 0x18BC20: ('dispatch', 0x4A0), 0x190F20: ('trigger', 0xD4),
    0x18C0C0: ('target_copy', 0x10), 0x1914A0: ('settle', 0x8C), 0x191580: ('settle_body', 0x13C),
    0x18C5A0: ('height_5a0', 0xF4), 0x1916C0: ('placement', 0x674), 0x191000: ('orient', 0x11C),
    0x22FCA0: ('boom', 0x358), 0x230000: ('tether', 0x224), 0x194D10: ('region', 0x94),
    0x18DD20: ('solve', 0x1B48), 0x18CE60: ('bounds_ce60', 0x4C4), 0x18D910: ('bounds_d910', 0x408),
    0x15CBA0: ('state_map', 0x3E8), 0x18F870: ('solve_aim', 0x16A4), 0x193D90: ('orbit', 0x118),
}
ENTRY = {name: address for address, (name, _) in MINE.items()}
# Translations of other modules executed unmodified in the EE here.
LEAVES = {0x18C6A0: 0x1A8, 0x18C4B0: 0xE8, 0x191D40: 0x2C4, 0x192010: 0x1B8, 0x191390: 0x108,
          0x1028D0: 0x14, 0x1028B8: 0x14, 0x102760: 0x34, 0x103230: 0x18, 0x102900: 0x18,
          0x102738: 0x24, 0x102948: 0xC, 0x1031E0: 0x1C, 0x11DF78: 0x1C}
TRANSLATED = set(MINE) | set(LEAVES)

# Original callee -> worker name (every other jal target of the routines above).
CALLEES = {
    0x1B1470: 'wrap', 0x1B1240: 'heading', 0x11E2A8: 'sine', 0x11DE90: 'cosine',
    0x11E620: 'atan2', 0x11E748: 'sqrt', 0x19A910: 'segment', 0x1B1EA0: 'inside',
    0x18D7B0: 'solve_dispatch', 0x18C0D0: 'commit', 0x22EEF0: 'w_0022EEF0', 0x1B0C60: 'w_001B0C60',
    0x195130: 'w_00195130', 0x197D20: 'w_00197D20', 0x198650: 'w_00198650', 0x198AF0: 'w_00198AF0',
    0x1936E0: 'w_001936E0', 0x18CA90: 'w_0018CA90', 0x198CE0: 'w_00198CE0', 0x198D90: 'w_00198D90',
    0x198F10: 'w_00198F10', 0x1963A0: 'w_001963A0', 0x196CE0: 'w_00196CE0', 0x197390: 'w_00197390',
    0x193EB0: 'w_00193EB0', 0x1DD980: 'w_001DD980', 0x1D2830: 'w_001D2830', 0x1B0300: 'w_001B0300',
    0x1B12B0: 'approach',
}
WORKERS = ('wrap', 'heading', 'sine', 'cosine', 'atan2', 'sqrt', 'segment', 'inside',
           'solve_dispatch', 'commit', 'w_0022EEF0', 'w_001B0C60',
           'w_00195130', 'w_00197D20', 'w_00198650', 'w_00198AF0', 'w_001936E0', 'w_0018CA90',
           'w_00198CE0', 'w_00198D90', 'w_00198F10', 'w_001963A0', 'w_00196CE0', 'w_00197390',
           'w_00193EB0', 'w_001DD980', 'w_001D2830', 'w_001B0300', 'approach')
WORKER_ID = {name: i for i, name in enumerate(WORKERS)}
HANDLERS = WORKERS[12:24]


def in_mine(pc):
    return any(start <= pc < start + size for start, (_, size) in MINE.items())


def jal_targets(elf):
    ee = EE(elf)
    targets = set()
    for start, (_, size) in MINE.items():
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


# Branch outcomes that no input can produce (docs/CAMERA_LEFTOVERS.md,
# "Unreachable outcomes"). The run asserts they never occur.
INFEASIBLE = {
    # 0018DD20: the side-A slide test only sees steep != 1 when the first
    # probe was dropped, and then 0x70003A3C is 0 (0018E8D8).
    (0x18E910, True), (0x18E930, True),
    # side B: steep != 1 there means the first probe was dropped
    # (0x70003A3C = 0) or side B rejected its hit (0x70003A3C = -1).
    (0x18EF00, True),
    # the pick: flag 2 is set whenever side A kept a hit (every side-A slide
    # sets it), so "flag 4 without flag 2" never has a side-A hit.
    (0x18F248, False),
    # 0018F870's wall slide (the same code, entered on the same condition:
    # steep, or the first probe missed).
    (0x190214, True), (0x190234, True), (0x190650, True), (0x190998, False),
}


def branch_sites(elf):
    ee = EE(elf)
    sites = set()
    for start, (_, size) in MINE.items():
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
HIT_POINT, HIT_POINTER, HIT_RECORD = 0x700031B0, 0x700031D0, 0x6E0000
SCRATCH_BASE, SCRATCH_SIZE = 0x700038A0, 0x1A0
TABLE_24A5F0, TABLE_WORDS = 0x24A5F0, 0x40
# name -> (original address, bytes)
REGIONS = {
    'cam': (CAM, 0xD0), 'player': (PLAYER, 0x320), 'other': (OTHER, 0x320),
    'eye': (0x8105D0, 16), 'target': (0x8105E0, 16), 'd5F0': (0x8105F0, 16),
    'd690': (0x810690, 4), 'd698': (0x810698, 4), 'd69C': (0x81069C, 4),
    'area': (0x810700, 1), 'd701': (0x810701, 1), 'd702': (0x810702, 1),
    'd6B8': (0x8106B8, 1), 'd6EF': (0x8106EF, 1), 'dE74': (0x810E74, 2),
    'd28A9A0': (0x28A9A0, 2), 's3B80': (0x70003B80, 2), 's3B8D': (0x70003B8D, 1),
    's31F0': (0x700031F0, 1), 'scratch': (SCRATCH_BASE, SCRATCH_SIZE),
}
COMPARED = sorted(a for base, size in REGIONS.values() for a in range(base, base + size))
COMPARED_SET = set(COMPARED)
RECORD_ID = {CAM: 'cam', PLAYER: 'player', OTHER: 'other'}


# ======================================================================
# The interpreter
# ======================================================================

class LeftEE(follow.CameraEE):
    """The follow test's CameraEE (FallEE plus two MMI forms and the write
    recorder), recording branch outcomes inside these routines."""

    def __init__(self, elf, ram=None, spad=None, cover=False):
        super().__init__(elf, ram, spad, cover=False)
        self.mine_cover = cover

    def branch(self, word, pc):
        b = follow.FallEE.branch(self, word, pc)
        if self.mine_cover and b is not None and in_mine(pc):
            self.outcomes.add((pc, b[0]))
        return b


# ======================================================================
# Native side
# ======================================================================

SHIM = r'''
#include "game/em_camera_leftovers.h"
#include <string.h>

typedef int (*py_cb)(int id, const uint32_t *in, int nin, uint32_t *out);
static py_cb PY;
static const void *IDS[8];
static EmCamLeftWorkers K;

static uint32_t rid(const void *p)
{
    for (uint32_t i = 0; i < 8; i++) if (IDS[i] && IDS[i] == p) return i;
    return 0xFFFFFFFFu;
}
static int call(int id, const uint32_t *in, int nin, uint32_t *out) { return PY(id, in, nin, out); }
#define F1(NAME, ID) static int t_##NAME(void *c, uint32_t x, uint32_t *out) \
    { (void)c; uint32_t in[1] = { x }; return call(ID, in, 1, out); }
F1(wrap, 0)
F1(sine, 2)
F1(cosine, 3)
F1(sqrt, 5)
static int t_heading(void *c, const uint32_t o[3], uint32_t x, uint32_t z, uint32_t *out)
{ (void)c; uint32_t in[5] = { o[0], o[1], o[2], x, z }; return call(1, in, 5, out); }
static int t_atan2(void *c, uint32_t y, uint32_t x, uint32_t *out)
{ (void)c; uint32_t in[2] = { y, x }; return call(4, in, 2, out); }
static int t_segment(void *c, const uint32_t a[4], const uint32_t b[4], int mask, EmCamLeftHit *hit,
                     int *result)
{
    (void)c; uint32_t in[10], out[4] = { 0, 0, 0, 0 };
    memcpy(in, a, 16); memcpy(in + 4, b, 16); in[8] = (uint32_t)mask; in[9] = rid(hit);
    int r = call(6, in, 10, out);
    *result = (int)out[0];
    return r;
}
static int t_inside(void *c, int mode, const uint32_t p[3], uint32_t poly, int count, int *result)
{
    (void)c; uint32_t in[6] = { (uint32_t)mode, p[0], p[1], p[2], poly, (uint32_t)count }, out[4] = { 0 };
    int r = call(7, in, 6, out);
    *result = (int)out[0];
    return r;
}
static int t_solve_dispatch(void *c, EmCameraFollowRecord *cam, int style, int *result)
{
    (void)c; uint32_t in[2] = { rid(cam), (uint32_t)style }, out[4] = { 0 };
    int r = call(8, in, 2, out);
    *result = (int)out[0];
    return r;
}
static int t_commit(void *c, EmCameraFollowRecord *cam, int mode)
{ (void)c; uint32_t in[2] = { rid(cam), (uint32_t)mode }, out[4]; return call(9, in, 2, out); }
static int t_0022EEF0(void *c, EmCameraFollowRecord *cam, int a1)
{ (void)c; uint32_t in[2] = { rid(cam), (uint32_t)a1 }, out[4]; return call(10, in, 2, out); }
static int t_001B0C60(void *c, int a0, int a1, int a2)
{ (void)c; uint32_t in[3] = { (uint32_t)a0, (uint32_t)a1, (uint32_t)a2 }, out[4]; return call(11, in, 3, out); }
#define H(NAME, ID) static int t_##NAME(void *c, EmCameraFollowRecord *cam, EmPlayerLiveActor *e) \
    { (void)c; uint32_t in[2] = { rid(cam), rid(e) }, out[4]; return call(ID, in, 2, out); }
H(00195130, 12) H(00197D20, 13) H(00198650, 14) H(00198AF0, 15) H(001936E0, 16) H(0018CA90, 17)
H(00198CE0, 18) H(00198D90, 19) H(00198F10, 20) H(001963A0, 21) H(00196CE0, 22) H(00197390, 23)
static int t_00193EB0(void *c, EmCameraFollowRecord *cam, EmPlayerLiveActor *e, int a2)
{ (void)c; uint32_t in[3] = { rid(cam), rid(e), (uint32_t)a2 }, out[4]; return call(24, in, 3, out); }
static int t_001DD980(void *c, uint32_t *eye, uint32_t *target)
{ (void)c; uint32_t in[2] = { rid(eye), rid(target) }, out[4]; return call(25, in, 2, out); }
static int t_001D2830(void *c, int a0, int a1)
{ (void)c; uint32_t in[2] = { (uint32_t)a0, (uint32_t)a1 }, out[4]; return call(26, in, 2, out); }
static int t_001B0300(void *c) { (void)c; uint32_t out[4]; return call(27, NULL, 0, out); }
static int t_approach(void *c, uint32_t t, uint32_t cur, uint32_t r, uint32_t *out)
{ (void)c; uint32_t in[3] = { t, cur, r }; return call(28, in, 3, out); }

static void setup(py_cb cb, uint64_t missing, const void *const ids[8])
{
    PY = cb;
    for (int i = 0; i < 8; i++) IDS[i] = ids ? ids[i] : NULL;
    memset(&K, 0, sizeof K);
#define SET(FIELD, FN, ID) if (!(missing >> (ID) & 1)) K.FIELD = FN;
    SET(wrap, t_wrap, 0) SET(heading, t_heading, 1) SET(sine, t_sine, 2) SET(cosine, t_cosine, 3)
    SET(atan2, t_atan2, 4) SET(sqrt, t_sqrt, 5) SET(segment, t_segment, 6) SET(inside, t_inside, 7)
    SET(solve_dispatch, t_solve_dispatch, 8) SET(commit, t_commit, 9) SET(w_0022EEF0, t_0022EEF0, 10)
    SET(w_001B0C60, t_001B0C60, 11) SET(w_00195130, t_00195130, 12) SET(w_00197D20, t_00197D20, 13)
    SET(w_00198650, t_00198650, 14) SET(w_00198AF0, t_00198AF0, 15) SET(w_001936E0, t_001936E0, 16)
    SET(w_0018CA90, t_0018CA90, 17) SET(w_00198CE0, t_00198CE0, 18) SET(w_00198D90, t_00198D90, 19)
    SET(w_00198F10, t_00198F10, 20) SET(w_001963A0, t_001963A0, 21) SET(w_00196CE0, t_00196CE0, 22)
    SET(w_00197390, t_00197390, 23) SET(w_00193EB0, t_00193EB0, 24) SET(w_001DD980, t_001DD980, 25)
    SET(w_001D2830, t_001D2830, 26) SET(w_001B0300, t_001B0300, 27) SET(approach, t_approach, 28)
}

EmCamLeftWorkers *shim_workers(void) { return &K; }

static int run(int entry, EmCamLeftWorld *w, EmPlayerLiveActor *e, int a2, int a3, uint32_t f12,
               uint32_t f13, void *vec, int *v0);

/* One entry point with its own callback; the previous callback, ids and
 * worker table are restored afterwards (a worker may run a nested entry). */
int shim_run(py_cb cb, uint64_t missing, const void *const ids[8], int entry, EmCamLeftWorld *w,
             EmPlayerLiveActor *e, int a2, int a3, uint32_t f12, uint32_t f13, void *vec, int *v0)
{
    py_cb saved_py = PY;
    const void *saved_ids[8];
    EmCamLeftWorkers saved_k = K;
    memcpy(saved_ids, IDS, sizeof IDS);
    setup(cb, missing, ids);
    int r = run(entry, w, e, a2, a3, f12, f13, vec, v0);
    PY = saved_py;
    memcpy(IDS, saved_ids, sizeof IDS);
    K = saved_k;
    return r;
}

/* `vec` is the a0 / a1 vector where the original takes one. */
static int run(int entry, EmCamLeftWorld *w, EmPlayerLiveActor *e, int a2, int a3, uint32_t f12,
               uint32_t f13, void *vec, int *v0)
{
    uint32_t v[2];
    int r;
    switch (entry) {
    case 0: return em_camleft_0018B9C0(w);
    case 1: return em_camleft_0018BC20(w, e);
    case 2: return em_camleft_00190F20(w, e);
    case 3: return em_camleft_0018C0C0(w);
    case 4: return em_camleft_001914A0(w, e);
    case 5: return em_camleft_00191580(w, e);
    case 6:
        memcpy(v, vec, 8);
        r = em_camleft_0018C5A0(w, v, f12, f13, v0);
        memcpy(vec, v, 8);
        return r;
    case 7: return em_camleft_001916C0(w, e, a2);
    case 8: return em_camleft_00191000(w, e, v0);
    case 9: return em_camleft_0022FCA0(w);
    case 10: return em_camleft_00230000(w, e);
    case 11: return em_camleft_00194D10(w, e, a2, v0);
    case 12: return em_camleft_0018DD20(w, e, a2, a3, v0);
    case 13: return em_camleft_0018CE60(w, vec, a2);
    case 14: return em_camleft_0018D910(w, e, a2);
    case 15: return em_camleft_0015CBA0(e);
    case 16: return em_camleft_0018F870(w, e, a2, a3, v0);
    case 17: return em_camleft_00193D90(w, e);
    default: return -99;
    }
}
'''

ENTRIES = ('frame', 'dispatch', 'trigger', 'target_copy', 'settle', 'settle_body', 'height_5a0',
           'placement', 'orient', 'boom', 'tether', 'region', 'solve', 'bounds_ce60',
           'bounds_d910', 'state_map', 'solve_aim', 'orbit')
ENTRY_INDEX = {name: i for i, name in enumerate(ENTRIES)}
SOURCES = ('src/game/em_camera_leftovers.c', 'src/game/em_camera_leftovers_solver.c',
           'src/game/em_camera_follow_original.c', 'src/game/em_sdk_math_original.c')

U32, I, VP = C.c_uint32, C.c_int, C.c_void_p
P = C.POINTER
CB = C.CFUNCTYPE(I, I, P(U32), I, P(U32))


class LiveActor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', VP), ('link_prev', VP),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


class FollowGlobals(C.Structure):
    _fields_ = [(n, VP) for n in ('eye', 'target', 'd690', 'd698', 'd69C', 'area', 'd701', 'd702')]


class Globals(C.Structure):
    _fields_ = [('follow', VP)] + [(n, VP) for n in ('d5F0', 'd6EF', 'd6B8', 'dE74', 'd28A9A0',
                                                    's3B80', 's3B8D', 's31F0', 'd24A5F0')] + \
               [('d24A5F0_words', C.c_size_t)]


class Hit(C.Structure):
    _fields_ = [('point', U32 * 4), ('record_1A', C.c_uint16), ('normal', U32 * 3)]


class World(C.Structure):
    _fields_ = [('cam', VP), ('player', VP), ('globals', VP), ('scratch', VP), ('hit', VP),
                ('workers', VP), ('fault', U32)]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    shim = OUT / 'camera_leftovers_shim.c'
    shim.write_text(SHIM)
    lib = OUT / ('camera_leftovers.dylib' if sys.platform == 'darwin' else 'camera_leftovers.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', str(shim), *SOURCES,
                    '-o', str(lib)], cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.shim_workers.argtypes = []
    n.shim_workers.restype = VP
    n.shim_run.argtypes = [CB, C.c_uint64, P(VP), I, P(World), VP, I, I, U32, U32, VP, P(I)]
    n.shim_run.restype = I
    return n


class NativeState:
    """The native storage of every region, addressed by original address (so
    scripted effects apply identically on both sides), plus the hit mirror."""

    GLOBAL_PTRS = ('d5F0', 'd6EF', 'd6B8', 'dE74', 'd28A9A0', 's3B80', 's3B8D', 's31F0')
    FOLLOW_PTRS = ('eye', 'target', 'd690', 'd698', 'd69C', 'area', 'd701', 'd702')

    def __init__(self, table):
        self.buf = {}
        self.actor = {'player': LiveActor(), 'other': LiveActor()}
        for name, (_, size) in REGIONS.items():
            if name in self.actor:
                self.buf[name] = self.actor[name].bytes
            else:
                self.buf[name] = (U32 * ((size + 3) // 4))()
        self.hit = Hit()
        self.table = (U32 * len(table))(*table)
        self.follow = FollowGlobals(**{n: C.cast(self.buf[n], VP) for n in self.FOLLOW_PTRS})
        self.globals = Globals(follow=C.cast(C.pointer(self.follow), VP),
                               d24A5F0=C.cast(self.table, VP), d24A5F0_words=len(table),
                               **{n: C.cast(self.buf[n], VP) for n in self.GLOBAL_PTRS})
        self.addresses = sorted((base, size, name) for name, (base, size) in REGIONS.items())

    def ptr(self, name):
        return C.addressof(self.buf[name]) if name not in self.actor else C.addressof(self.actor[name])

    def slot(self, address):
        for base, size, name in self.addresses:
            if base <= address < base + size:
                return name, address - base
        raise AssertionError(('address the native state does not hold', hex(address)))

    def save(self, address, value, size=4):
        for i in range(size):
            name, at = self.slot(address + i)
            C.memmove(C.addressof(self.buf[name]) + at, bytes([value >> (8 * i) & 0xFF]), 1)

    def load(self, address, size=4):
        out = 0
        for i in range(size):
            name, at = self.slot(address + i)
            out |= C.string_at(C.addressof(self.buf[name]) + at, 1)[0] << (8 * i)
        return out

    def write(self, name, data):
        C.memmove(C.addressof(self.buf[name]), bytes(data), len(data))

    def read(self, name):
        return C.string_at(C.addressof(self.buf[name]), REGIONS[name][1])

    def image(self):
        return b''.join(self.read(name) for name in sorted(REGIONS, key=lambda n: REGIONS[n][0]))

    def set_hit(self, point, record_1A, normal):
        for i in range(4): self.hit.point[i] = point[i]
        self.hit.record_1A = record_1A
        for i in range(3): self.hit.normal[i] = normal[i]


def ee_image(ee):
    return b''.join(ee.read(base, size) for _, (base, size) in
                    sorted(REGIONS.items(), key=lambda kv: kv[1][0]))


def image_diff(a, b):
    order = []
    for _, (base, size) in sorted(REGIONS.items(), key=lambda kv: kv[1][0]):
        order.extend(range(base, base + size))
    return [hex(order[i]) for i in range(len(order)) if a[i] != b[i]]


# ======================================================================
# Scripted callee effects (identical on both sides)
# ======================================================================

ANGLES = (0.0, -0.0, 0.5, -0.5, 3.14159274, -3.14159274, 1.5707964, 0.0523599, -0.0523599,
          0.05235988, 0.0523598)
RECORD_FLAGS = (0, 0x8800, 0x0800, 0x8000, 0x2000, 0x5000, 0x1000, 0x4000, 0x7000, 0x2800,
                0xA000, 0xD800, 0x4800, 0x1800, 0x3000)
CAM_WORDS = (0x0C, 0x10, 0x14, 0x18, 0x20, 0x24, 0x28, 0x44, 0x48, 0x50, 0x54, 0x5C, 0x60, 0x8C, 0x90)
CAM_BYTES = (0, 1, 2, 3, 4, 5, 6, 7, 0x6C, 0x6D)


def angle(rng):
    return F(rng.choice(ANGLES) if rng.random() < 0.3 else rng.uniform(-3.2, 3.2))


def unit(v):
    n = math.sqrt(sum(x * x for x in v))
    return [x / n for x in v] if n > 1e-9 else [0.0, 1.0, 0.0]


def segment_effect(rng, entry, memory):
    a = [number(w) for w in entry[1]]
    b = [number(w) for w in entry[2]]
    finite = all(math.isfinite(x) and abs(x) < 1e7 for x in a[:3] + b[:3])
    ret = rng.choice((0, 0, 1, 1, 1, 2, 4))
    if ret == 0 and rng.random() < 0.5:
        return ret, None                                  # the hit words stay as they were
    flags = rng.choice(RECORD_FLAGS) if rng.random() < 0.9 else rng.randrange(0x10000)
    pick = rng.random()
    if memory and rng.random() < 0.25:
        sign = rng.choice((1.0, -1.0))                    # the first probe's surface, or facing it
        normal = [sign * x for x in memory[0]]
    elif finite and pick < 0.25:
        d = unit([a[i] - b[i] for i in range(3)])         # facing the segment
        normal = [x * rng.choice((1.0, -1.0)) for x in d]
    elif finite and pick < 0.4:
        d = unit([a[0] - b[0], 0.0, a[2] - b[2]])
        normal = [x * rng.choice((1.0, -1.0)) for x in d]
    elif memory and pick < 0.6:
        # a surface related to an earlier one (the first probe's normal is
        # what the side probes compare against)
        prev = memory[0] if rng.random() < 0.6 else memory[-1]
        sign = rng.choice((1.0, 1.0, -1.0, -1.0, -1.0))
        bend = rng.choice((0.0, 0.0, 0.0, 0.02, 0.05, 0.2, 0.5, 0.8, 1.2))
        normal = unit([sign * prev[i] + bend * rng.uniform(-1, 1) for i in range(3)])
    elif pick < 0.75:
        axis = rng.randrange(3)
        normal = [0.0, 0.0, 0.0]
        normal[axis] = rng.choice((1.0, -1.0))
        if rng.random() < 0.5:
            tilt = rng.choice((0.1, 0.2, 0.3, 0.45, 0.5, 0.9))
            normal = unit([x + tilt * rng.uniform(-1, 1) for x in normal])
    else:
        normal = unit([rng.uniform(-1, 1) for _ in range(3)])
    memory.append(normal)
    pick = rng.random()
    if finite and pick < 0.45:
        off = rng.choice((0.0, 0.1, 0.4, 0.7, 1.1, 2.0, 5.0))
        point = [b[i] + off * rng.uniform(-1, 1) for i in range(3)]
    elif finite and pick < 0.8:
        t = rng.random()
        point = [a[i] + t * (b[i] - a[i]) for i in range(3)]
    else:
        point = [rng.uniform(-400, 400) for _ in range(3)]
    point = [F(x) for x in point] + [rng.choice((F(1.0), 0, rng.getrandbits(32)))]
    return ret, (point, flags, [F(x) for x in normal])


def effect_for(rng, name, entry, memory):
    """What a hooked callee does: return value (v0), f0, the writes it makes
    (address, size, value) and, for segment, the hit words it leaves."""
    e = {'ret': 0, 'fret': None, 'writes': [], 'hit': None}
    w = e['writes']
    chance = rng.random
    if name == 'wrap':
        pick = chance()
        e['fret'] = entry[1] if pick < 0.5 else angle(rng) if pick < 0.85 else rng.choice((0, 0x80000000))
    elif name in ('heading', 'atan2'):
        e['fret'] = angle(rng)
    elif name == 'approach':
        pick = chance()
        e['fret'] = entry[1] if pick < 0.45 else entry[2] if pick < 0.6 else angle(rng)
    elif name in ('sine', 'cosine'):
        e['fret'] = F(rng.choice((0.0, 1.0, -1.0, 0.70710677))) if chance() < 0.3 else F(rng.uniform(-1, 1))
    elif name == 'sqrt':
        e['fret'] = F(rng.choice((25.0, 15.0, 8.0, 6.9, 0.0, -25.0, -15.0, -12.0, -8.0, -6.9, -20.0,
                                  -10.0, -7.0))) if chance() < 0.5 else F(rng.uniform(-40, 60))
    elif name == 'segment':
        e['ret'], e['hit'] = segment_effect(rng, entry, memory)
    elif name == 'inside':
        e['ret'] = rng.choice((0, 0, 1, 1, 2))
    else:
        # the camera callees: writes into the camera block and the globals
        for _ in range(rng.choice((0, 0, 1, 2, 3))):
            w.append((CAM + rng.choice(CAM_WORDS), 4, F(rng.uniform(-400, 400))))
        if chance() < 0.25:
            w.append((CAM + rng.choice(CAM_BYTES), 1, rng.choice((0, 1, 2, 3, 7, 8, 0xB))))
        if chance() < 0.2:
            w.append((0x8105D0 + 4 * rng.randrange(8), 4, F(rng.uniform(-400, 400))))
        if chance() < 0.15:
            w.append((SCRATCH_BASE + 4 * rng.randrange(SCRATCH_SIZE // 4), 4, F(rng.uniform(-2, 2))))
        if name in ('solve_dispatch',):
            e['ret'] = rng.choice((0, 1, 8, 0x40, 0x80, rng.randrange(256)))
    if chance() < 0.05:
        # a state change behind the call: the routines must re-read +230
        state = rng.choice((1, 2, 3, 5, 0xA, 0xF, 0x11, 0x12))
        w.extend(((PLAYER + 0x230, 4, state), (OTHER + 0x230, 4, state)))
    return e


class Script:
    def __init__(self, seed, fail_at=None):
        self.seed, self.count, self.fail_at, self.memory = seed, 0, fail_at, []

    def next(self, name, entry):
        rng = random.Random('%d:%d:%s:%r' % (self.seed, self.count, name, entry))
        index = self.count
        self.count += 1
        return index, effect_for(rng, name, entry, self.memory)


# ======================================================================
# Cases
# ======================================================================

WEIGHTS = {'frame': 10, 'dispatch': 10, 'trigger': 4, 'target_copy': 1, 'settle': 4,
           'settle_body': 4, 'height_5a0': 4, 'placement': 12, 'orient': 6, 'boom': 8,
           'tether': 8, 'region': 4, 'solve': 40, 'bounds_ce60': 8, 'bounds_d910': 8, 'solve_aim': 36,
           'orbit': 4}
ENTRY_POOL = tuple(name for name, n in WEIGHTS.items() for _ in range(n))
PLAYER_CODES = tuple(range(0x31)) + (1, 1, 3, 3, 2, 4, 0xF, 5, 0xA, 0x11, 0x12, 0x80, -1)


def put(buf, offset, size, value):
    buf[offset:offset + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')


def get_f(buf, offset):
    return struct.unpack_from('<f', buf, offset)[0]


def make_case(seed):
    rng = random.Random(seed)
    choose = lambda values: F(rng.choice(values))
    entry = rng.choice(ENTRY_POOL)
    cam = bytearray(rng.getrandbits(8) for _ in range(0xD0))
    player = bytearray(rng.getrandbits(8) for _ in range(0x320))
    wild = rng.random() < 0.03
    base = [rng.uniform(-400, 400) for _ in range(3)]
    if not wild:
        for i, off in enumerate((0x10, 0x14, 0x18)):
            put(cam, off, 4, F(base[i] + rng.uniform(-40, 40)))
        for i, off in enumerate((0x20, 0x24, 0x28)):
            put(cam, off, 4, F(base[i] + rng.choice((0.0, 0.5, -0.5, 3.0, -30.0, 30.0))))
        put(cam, 0x1C, 4, rng.choice((F(1.0), 0, rng.getrandbits(32))))
        put(cam, 0x2C, 4, rng.choice((F(1.0), 0, rng.getrandbits(32))))
        for off in (0x30, 0x34, 0x38, 0x3C, 0x48, 0x4C, 0x60, 0x94):
            put(cam, off, 4, F(rng.uniform(-400, 400)))
        put(cam, 0x0C, 4, choose((-30.0, -46.8, 30.0, -20.0, -60.0, 0.0, -10.0, 5.0)) if rng.random() < 0.7
            else F(rng.uniform(-80, 80)))
        put(cam, 0x44, 4, angle(rng))
        put(cam, 0x90, 4, angle(rng))
        put(cam, 0x50, 4, F(base[1] + rng.choice((-200.0, -20.0, -3.0, 0.0, 3.0))))
        put(cam, 0x54, 4, F(base[1] + rng.choice((1000.0, 200.0, 30.0, 3.0, 0.0, -5.0))))
        put(cam, 0x5C, 4, choose((1.0, 2.0, 6.0, 0.0)) if rng.random() < 0.7 else F(rng.uniform(-8, 8)))
        put(cam, 0x64, 4, rng.choice((F(-46.8), F(-30.0), F(-31.2), 0xC1F99999, F(46.8), F(5.0), F(-5.0))))
        put(cam, 0x8C, 4, choose((6.0, 2.0, 0.0, -3.0, 11.0)) if rng.random() < 0.7 else F(rng.uniform(-12, 12)))
        put(cam, 0x98, 4, choose((0.0, 23.0, -2.0)))
        for i, off in enumerate((0xA0, 0xA4, 0xA8)):
            put(player, off, 4, F(base[i] + rng.uniform(-30, 30)))
        put(player, 0xAC, 4, rng.choice((F(1.0), 0, rng.getrandbits(32))))
        for i, off in enumerate((0xB0, 0xB4, 0xB8)):
            put(player, off, 4, F(get_f(player, 0xA0 + 4 * i) + rng.choice((0.0, 4.0, -4.0, 10.0))))
        put(player, 0xBC, 4, rng.choice((F(1.0), 0, rng.getrandbits(32))))
        put(player, 0xC4, 4, angle(rng))
    put(cam, 0, 1, rng.choice((0, 1, 1, 1, 2)))
    put(cam, 1, 1, rng.choice((0, 1, 1, 2, 3)))
    put(cam, 3, 1, rng.choice((0, 0, 1, 2, 7)))
    put(cam, 4, 1, rng.choice((0, 0, 0, 1, 2, 3, 4)))
    put(cam, 5, 1, rng.choice((0, 0, 0, 1, 1, 2)))
    put(cam, 6, 1, rng.choice(tuple(range(17)) + (0, 0, 0, 0xA, 0xD, 0xF, 0x40)))
    put(cam, 7, 1, rng.choice((0, 1, 8, rng.randrange(256))))
    put(cam, 0x58, 2, rng.choice((0, 0x8800, 0x2000, 0x0800, 0x8000, 0x5000, 0xD800, 0x1000, rng.randrange(0x10000))))
    put(cam, 0xA0, 2, rng.choice((0, 0, 1, 2, 5, 0xFFFF, 0x8000)))
    state = rng.choice(PLAYER_CODES)
    put(player, 0x230, 4, state)
    put(player, 0x1F0, 1, rng.choice((6, 6, 0, 1, rng.randrange(256))))
    put(player, 0x4, 1, rng.choice((5, 0, 1)))
    other = bytearray(player)
    use_other = rng.random() < 0.25
    if use_other:
        put(other, 0x230, 4, rng.choice(PLAYER_CODES))
        for off in (0xA0, 0xA4, 0xA8, 0xB4, 0xC4):
            if rng.random() < 0.5:
                put(other, off, 4, F(rng.uniform(-3, 3) if off == 0xC4 else rng.uniform(-400, 400)))
    g = {}
    g['eye'] = [F(base[i] + rng.uniform(-40, 40)) for i in range(3)] + [rng.choice((F(1.0), rng.getrandbits(32)))]
    g['target'] = [F(get_f(cam, 0x20 + 4 * i) + rng.choice((0.0, 0.1, -0.1, 0.2, 5.0))) if not wild
                   else rng.getrandbits(32) for i in range(3)] + [rng.getrandbits(32)]
    g['d5F0'] = [rng.getrandbits(32) for _ in range(4)]
    boom = abs(get_f(cam, 0x0C)) if not wild else 30.0
    if boom != boom or boom > 1e6:
        boom = 30.0
    g['d690'] = F(boom + rng.choice((5.0, 0.5, 0.0, -0.5, -9.0, -10.0, -10.5, -15.0, -19.0, -20.0,
                                     -20.5, -25.0, -30.0, -60.0))) if rng.random() < 0.85 \
        else choose((0.5, 1.0, 0.0, 40.0))
    g['d698'] = choose((10.0, 23.3, 30.0))
    g['d69C'] = F(boom + rng.choice((0.0, -9.0, -10.5, -12.0, -20.5, -25.0, -40.0))) if rng.random() < 0.4 \
        else choose((5.0, 6.9, 7.0, 7.1, 8.6, 8.6000004, 12.0, -9.0, -50.0, 60.0))
    g['area'] = rng.choice((0xB, 0xB, 0xB, 0, 0x12, 0x12, 0xE, 0x15, 3, 0x10, 0x13, rng.randrange(256)))
    g['d701'] = rng.choice((0, 1, 2))
    g['d702'] = rng.choice((0, 0, 1, 5, 6))
    g['d6B8'] = rng.choice((0, 0, 0, 1))
    g['d6EF'] = rng.choice((0, 1, 5))
    g['dE74'] = rng.choice((0, 0x400, 0xFFFF))
    g['s3B80'] = rng.choice((0, 0x400, 0x0404, 0x1000))
    g['d28A9A0'] = rng.choice((0, 0, 1, -1))
    g['s3B8D'] = rng.choice((0, 0, 1))
    g['s31F0'] = rng.choice((0, 0x10, 0x81))
    scratch = [rng.getrandbits(32) for _ in range(SCRATCH_SIZE // 4)]
    for off in range(0, SCRATCH_SIZE, 4):
        if rng.random() < 0.7:
            scratch[off // 4] = F(rng.uniform(-400, 400))
    hit = ([F(rng.uniform(-400, 400)) for _ in range(3)] + [rng.getrandbits(32)],
           rng.randrange(0x10000), [F(x) for x in unit([rng.uniform(-1, 1) for _ in range(3)])])
    case = {'seed': seed, 'entry': entry, 'state': state, 'use_other': use_other, 'globals': g,
            'scratch': scratch, 'hit': hit, 'a2': 0, 'a3': 0, 'f12': 0, 'f13': 0, 'vec': None}
    # entry-specific arguments near their thresholds
    target = other if use_other else player
    if entry == 'placement':
        case['a2'] = rng.choice((0, 0, 1, 2, 2))
        if not wild and rng.random() < 0.4:
            # cam+24 within the chase's 1.0 band of the placement height, so
            # the height's last bits reach the record
            y = get_f(target, rng.choice((0xA4, 0xB4))) + get_f(cam, 0x8C) + rng.choice((0.0, 11.0))
            put(cam, 0x24, 4, F(y + rng.uniform(-1.0, 1.0)))
        elif rng.random() < 0.4:
            # cam+24 near the actual target's height (the 0.15 settle test)
            put(cam, 0x24, 4, F(number(g['target'][1]) + rng.choice((0.0, 0.1, 0.15, 0.2, -0.1, -0.16))))
    elif entry == 'orient':
        if rng.random() < 0.5:
            g['dE74'], g['s3B80'] = 0x400, 0x400
        put(target, 0x1F0, 1, rng.choice((6, 0)))
    elif entry == 'region':
        case['a2'] = rng.choice((1, 1, 0, 2))
        index = case['a2']
        ref = number(TABLE[index * 16 + 1]) if TABLE and index * 16 + 1 < len(TABLE) else 0.0
        put(target, 0xA4, 4, F(ref + rng.choice((0.0, 3.9, 4.0, 4.1, -3.9, -4.0, -4.1, 20.0))))
    elif entry == 'tether':
        if rng.random() < 0.3:
            put(target, 0xA4, 4, choose((-83.0, -83.1, -82.9, -100.0, 0.0)))
        elif TABLE and rng.random() < 0.5:
            # near region 1's reference height (00194D10's 4.0 band)
            put(target, 0xA4, 4, F(number(TABLE[17]) + rng.choice((0.0, 3.9, -3.9, 4.1, 30.0))))
        put(target, 0x230, 4, rng.choice((2, 0xF, 4, 1, 3)))
    elif entry in ('trigger', 'dispatch'):
        if rng.random() < 0.5:
            put(target, 0xA0, 4, choose((285.0, 285.00003, 284.99997, 100.0, 400.0)))
        if entry == 'dispatch':
            put(target, 0x230, 4, rng.choice((5, 0xA, 8, 9, 7, 6, 0x2D, 0x2C, 0x11, 0x12, 1, 3)))
            put(cam, 6, 1, rng.randrange(18))
            if rng.random() < 0.5:
                # mode 1's locomotion path
                put(cam, 5, 1, 1)
                put(cam, 6, 1, rng.choice((0, 9, 0xB, 0xE, 0x20)))
                put(cam, 1, 1, rng.choice((0, 1, 1, 2)))
    elif entry in ('settle', 'settle_body') and not wild and rng.random() < 0.6:
        # cam+14 within the 0018C5A0 1.0 band of its goal
        y = 11.0 + get_f(cam, 0x8C) + get_f(cam, 0x5C) + get_f(target, 0xA4) + get_f(cam, 0x98)
        put(cam, 0x14, 4, F(y + rng.uniform(-1.0, 1.0)))
    elif entry in ('settle', 'settle_body') and not wild and rng.random() < 0.6:
        # cam+14 within the 0018C5A0 1.0 band of its goal
        y = 11.0 + get_f(cam, 0x8C) + get_f(cam, 0x5C) + get_f(target, 0xA4) + get_f(cam, 0x98)
        put(cam, 0x14, 4, F(y + rng.uniform(-1.0, 1.0)))
    elif entry == 'height_5a0':
        case['f12'] = F(get_f(cam, 0x14) - get_f(cam, 0x98) + rng.choice((0.0, 0.5, -0.5, 1.0, -1.0, 8.0, -8.0, 40.0))) \
            if not wild else rng.getrandbits(32)
        case['f13'] = choose((4.0, 1.0, 0.5, 100.0))
    elif entry == 'solve':
        case['a2'] = rng.choice((0, 0, 1, 3, 3, 4, 7, 2, 5))
        case['a3'] = rng.choice((6, 7))
    elif entry == 'solve_aim':
        case['a2'] = rng.choice((2, 2, 2, 6, 6, 0, 5))
        case['a3'] = rng.choice((7, 7, 6))
    if entry in ('solve', 'solve_aim') and not wild and rng.random() < 0.4:
        # the actor on the eye -> target line (the first probe then faces the
        # view, which is what the not-steep side push needs)
        t = rng.uniform(0.5, 2.0)
        for i in range(3):
            eye_i, tgt_i = get_f(cam, 0x10 + 4 * i), get_f(cam, 0x20 + 4 * i)
            put(target, 0xB0 + 4 * i, 4, F(eye_i + t * (tgt_i - eye_i)))
    if entry == 'frame' and cam[0] == 0 and rng.random() < 0.5:
        g['area'] = 0x12
    if entry == 'orbit':
        put(target, 0x230, 4, rng.choice((1, 2, 3, 1, 2)))
        if not wild and rng.random() < 0.5:
            word44 = struct.unpack_from('<I', cam, 0x44)[0]
            put(cam, 0x48, 4, rng.choice((word44, F(get_f(cam, 0x44) + 0.001))))
        put(cam, 7, 1, rng.choice((0, 1, 2, 4, 8, 0xD, 0xB)))
    elif entry == 'bounds_ce60':
        case['a2'] = rng.choice((0, 2, 5, 6))
        case['vec'] = rng.choice(('player', 'scratch'))
    elif entry == 'bounds_d910':
        case['a3'] = case['a2'] = rng.choice((6, 7))
    elif entry == 'frame':
        if rng.random() < 0.5:
            put(cam, 4, 1, 0)
            put(cam, 0, 1, 1)
    case['cam'], case['player'], case['other'] = bytes(cam), bytes(player), bytes(other)
    return case


TABLE = []          # D_0024A5F0 words, read from the ELF at start


# ======================================================================
# The oracle
# ======================================================================

def log_record(address):
    return RECORD_ID.get(address, hex(address))


class UnitOracle:
    def __init__(self, elf):
        self.ee = LeftEE(elf, cover=True)
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
                point, flags, normal = e['hit']
                for i in range(4): ee.save(HIT_POINT + 4 * i, point[i])
                ee.save(HIT_RECORD + 0x1A, flags, 2)
                for i in range(3): ee.save(HIT_RECORD + 0x24 + 4 * i, normal[i])
            ee.in_hook = False
            if e['fret'] is not None:
                ee.f[0] = e['fret']
            ee.ret_int(e['ret'])
        return run

    def log_entry(self, name, ee):
        a = ee.arg
        f = lambda i: ee.f[12 + i] & MASK
        vec = lambda address, n: tuple(ee.load(address + 4 * i) for i in range(n))
        if name in ('wrap', 'sine', 'cosine', 'sqrt'): return (name, f(0))
        if name == 'heading': return (name, vec(a(0), 3), f(0), f(1))
        if name == 'atan2': return (name, f(0), f(1))
        if name == 'segment': return (name, vec(a(0), 4), vec(a(1), 4), s32(a(2)))
        if name == 'inside': return (name, s32(a(0)), vec(a(1), 3), a(2), s32(a(3)))
        if name in ('solve_dispatch', 'commit', 'w_0022EEF0'): return (name, log_record(a(0)), s32(a(1)))
        if name == 'w_001B0C60': return (name, s32(a(0)), s32(a(1)), s32(a(2)))
        if name in HANDLERS: return (name, log_record(a(0)), log_record(a(1)))
        if name == 'w_00193EB0': return (name, log_record(a(0)), log_record(a(1)), s32(a(2)))
        if name == 'w_001DD980':
            return (name, {0x8105D0: 'eye'}.get(a(0), hex(a(0))), {0x8105E0: 'target'}.get(a(1), hex(a(1))))
        if name == 'w_001D2830': return (name, s32(a(0)), s32(a(1)))
        if name == 'w_001B0300': return (name,)
        if name == 'approach': return (name, f(0), f(1), f(2))
        raise AssertionError(name)

    def load_case(self, case):
        ee = self.ee
        ee.write(CAM, case['cam'])
        ee.write(PLAYER, case['player'])
        ee.write(OTHER, case['other'])
        g = case['globals']
        for name in ('eye', 'target', 'd5F0'):
            for i, value in enumerate(g[name]): ee.save(REGIONS[name][0] + 4 * i, value)
        for name in ('d690', 'd698', 'd69C'):
            ee.save(REGIONS[name][0], g[name])
        for name in ('area', 'd701', 'd702', 'd6B8', 'd6EF', 's3B8D', 's31F0'):
            ee.save(REGIONS[name][0], g[name], 1)
        for name in ('dE74', 'd28A9A0', 's3B80'):
            ee.save(REGIONS[name][0], g[name], 2)
        for i, value in enumerate(case['scratch']): ee.save(SCRATCH_BASE + 4 * i, value)
        point, flags, normal = case['hit']
        for i in range(4): ee.save(HIT_POINT + 4 * i, point[i])
        ee.save(HIT_POINTER, HIT_RECORD)
        ee.save(HIT_RECORD + 0x1A, flags, 2)
        for i in range(3): ee.save(HIT_RECORD + 0x24 + 4 * i, normal[i])

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
        entry, c = case['entry'], case
        address = ENTRY[entry]
        if entry in ('frame', 'target_copy', 'boom'):
            ee.call(address, (CAM,))
        elif entry in ('dispatch', 'trigger', 'settle', 'settle_body', 'orient', 'tether', 'orbit'):
            ee.call(address, (CAM, a1))
        elif entry == 'height_5a0':
            ee.f[12], ee.f[13] = c['f12'], c['f13']
            ee.call(address, (CAM + 0x10,))
        elif entry in ('placement', 'region', 'bounds_d910'):
            ee.call(address, (CAM, a1, c['a2']))
        elif entry in ('solve', 'solve_aim'):
            ee.call(address, (CAM, a1, c['a2'], c['a3']))
        elif entry == 'bounds_ce60':
            ee.call(address, (CAM, PLAYER + 0xB0 if c['vec'] == 'player' else 0x700038D0, c['a2']))
        elif entry == 'state_map':
            ee.call(address, (a1,))
        written = ee.writes
        ee.writes = None
        stray = sorted(a for a in written if a not in COMPARED_SET)
        assert not stray, (case['seed'], entry, 'original writes outside the compared set',
                           [hex(a) for a in stray[:16]])
        return {'image': ee_image(ee), 'log': self.log, 'v0': s32(ee.r[2])}


class NativeRun:
    """em_camera_leftovers*.c with Python workers replaying the script."""

    RETURNS = {'orient', 'region', 'solve', 'height_5a0', 'solve_aim'}

    def __init__(self, native, case, script, missing=()):
        self.native, self.case, self.script, self.log = native, case, script, []
        s = self.state = NativeState(TABLE)
        s.write('cam', case['cam'])
        s.write('player', case['player'])
        s.write('other', case['other'])
        g = case['globals']
        for name in ('eye', 'target', 'd5F0'):
            for i, value in enumerate(g[name]): s.save(REGIONS[name][0] + 4 * i, value)
        for name in ('d690', 'd698', 'd69C'):
            s.save(REGIONS[name][0], g[name])
        for name in ('area', 'd701', 'd702', 'd6B8', 'd6EF', 's3B8D', 's31F0'):
            s.save(REGIONS[name][0], g[name], 1)
        for name in ('dE74', 'd28A9A0', 's3B80'):
            s.save(REGIONS[name][0], g[name], 2)
        for i, value in enumerate(case['scratch']): s.save(SCRATCH_BASE + 4 * i, value)
        s.set_hit(*case['hit'])
        self.ids = (VP * 8)(s.ptr('cam'), s.ptr('player'), s.ptr('other'), s.ptr('eye'), s.ptr('target'),
                            C.addressof(s.hit), None, None)
        self.names = {0: 'cam', 1: 'player', 2: 'other', 3: 'eye', 4: 'target', 5: 'hit'}
        self.callback = CB(self.dispatch)
        self.mask = 0
        for name in missing:
            if name in WORKER_ID:
                self.mask |= 1 << WORKER_ID[name]
        self.workers = native.shim_workers()
        self.error = None
        self.world = World(cam=s.ptr('cam'), player=s.ptr('player'), globals=C.addressof(s.globals),
                           scratch=s.ptr('scratch'), hit=C.addressof(s.hit), workers=self.workers,
                           fault=0)
        for name in missing:
            if name in ('cam', 'player', 'globals', 'scratch', 'hit', 'workers'):
                setattr(self.world, name, None)
            elif name.startswith('g:'):
                field = name[2:]
                if field in NativeState.FOLLOW_PTRS:
                    setattr(s.follow, field, None)
                elif field == 'follow':
                    s.globals.follow = None
                else:
                    setattr(s.globals, field, None)

    def rid(self, value):
        return self.names.get(value, hex(value))

    def dispatch(self, wid, inp, nin, out):
        try:
            name = WORKERS[wid]
            args = [inp[i] for i in range(nin)]
            if name in ('wrap', 'sine', 'cosine', 'sqrt'): entry = (name, args[0])
            elif name == 'heading': entry = (name, tuple(args[:3]), args[3], args[4])
            elif name == 'atan2': entry = (name, args[0], args[1])
            elif name == 'segment':
                assert self.rid(args[9]) == 'hit', 'segment hit pointer is not the world hit'
                entry = (name, tuple(args[:4]), tuple(args[4:8]), s32(args[8]))
            elif name == 'inside': entry = (name, s32(args[0]), tuple(args[1:4]), args[4], s32(args[5]))
            elif name in ('solve_dispatch', 'commit', 'w_0022EEF0'): entry = (name, self.rid(args[0]), s32(args[1]))
            elif name == 'w_001B0C60': entry = (name, s32(args[0]), s32(args[1]), s32(args[2]))
            elif name in HANDLERS: entry = (name, self.rid(args[0]), self.rid(args[1]))
            elif name == 'w_00193EB0': entry = (name, self.rid(args[0]), self.rid(args[1]), s32(args[2]))
            elif name == 'w_001DD980': entry = (name, self.rid(args[0]), self.rid(args[1]))
            elif name == 'w_001D2830': entry = (name, s32(args[0]), s32(args[1]))
            elif name == 'approach': entry = (name, args[0], args[1], args[2])
            else: entry = (name,)
            self.log.append(entry)
            index, e = self.script.next(name, entry)
            if self.script.fail_at == index:
                return -1
            for address, size, value in e['writes']:
                self.state.save(address, value, size)
            if e['hit'] is not None:
                self.state.set_hit(*e['hit'])
            out[0] = e['fret'] if e['fret'] is not None else e['ret'] & MASK
            return 0
        except BaseException as error:           # surfaced after the native call
            self.error = error
            return -1

    def run(self):
        c, s = self.case, self.state
        entry = c['entry']
        e = s.ptr('other' if c['use_other'] else 'player')
        v0 = C.c_int(-99)
        vec = None
        if entry == 'height_5a0':
            vec = s.ptr('cam') + 0x10
        elif entry == 'bounds_ce60':
            vec = s.ptr('player') + 0xB0 if c['vec'] == 'player' else s.ptr('scratch') + (0x700038D0 - SCRATCH_BASE)
        status = self.native.shim_run(self.callback, self.mask, self.ids, ENTRY_INDEX[entry],
                                      C.byref(self.world), e, c['a2'], c['a3'], c['f12'], c['f13'],
                                      vec, C.byref(v0))
        if self.error is not None:
            raise self.error
        return status, {'image': s.image(), 'log': self.log,
                        'v0': v0.value if entry in self.RETURNS else None, 'fault': self.world.fault}


ELF = NATIVE = ORACLE = None


def run_case(seed):
    global ORACLE
    if ORACLE is None:
        ORACLE = UnitOracle(ELF)
    case = make_case(seed)
    want = ORACLE.run(case, Script(seed))
    status, got = NativeRun(NATIVE, case, Script(seed)).run()
    where = (seed, case['entry'], hex(case['state'] & MASK), case['use_other'])
    assert status == 0, (where, 'native fault', status, hex(got['fault']))
    assert want['log'] == got['log'], (where, 'worker calls', want['log'], got['log'])
    if want['image'] != got['image']:
        raise AssertionError((where, 'state differs at', image_diff(want['image'], got['image'])[:24]))
    if got['v0'] is not None:
        assert want['v0'] == got['v0'], (where, 'return', want['v0'], got['v0'])
    faults = 0
    if want['log'] and seed % 4 == 0:
        k = random.Random(seed).randrange(len(want['log']))
        status, cut = NativeRun(NATIVE, case, Script(seed, fail_at=k)).run()
        assert status == -1, (where, 'fault not reported', k)
        assert cut['log'] == want['log'][:k + 1], (where, 'calls after a fault', k)
        assert cut['fault'] == [a for a, n in CALLEES.items() if n == want['log'][k][0]][0], \
            (where, 'fault address', hex(cut['fault']))
        faults = 1
    called = {entry[0] for entry in want['log']}
    return (case['entry'], len(want['log']), faults, tuple(sorted(ORACLE.ee.outcomes)), tuple(sorted(called)))


# ======================================================================
# Refusals: missing workers, globals and records
# ======================================================================

GLOBAL_NAMES = tuple('g:' + n for n in NativeState.FOLLOW_PTRS + NativeState.GLOBAL_PTRS) + ('g:d24A5F0', 'g:follow')


def refusal_job(seed):
    """For one case: every worker, global pointer and record missing in turn.
    A worker the full run called must be refused before any write or call;
    one it did not call may be refused the same way or leave the run
    unchanged."""
    case = make_case(seed)
    full_status, full = NativeRun(NATIVE, case, Script(seed)).run()
    assert full_status == 0
    called = {entry[0] for entry in full['log']}
    refusals = 0
    for missing in WORKERS + GLOBAL_NAMES + ('cam', 'globals', 'scratch', 'hit', 'workers'):
        run = NativeRun(NATIVE, case, Script(seed), missing=(missing,))
        before = run.state.image()
        status, got = run.run()
        where = (seed, case['entry'], missing)
        if status == -1:
            assert got['log'] == [] and got['image'] == before, (where, 'refusal after a call or write',
                                                                 got['log'][:3], image_diff(before, got['image'])[:8])
            refusals += 1
        else:
            assert missing not in called, (where, 'a called worker was missing but the run went on')
            assert status == 0 and got['log'] == full['log'] and got['image'] == full['image'], \
                (where, 'a missing unused input changed the run')
    return refusals


# ======================================================================
# 0015CBA0: its whole input domain
# ======================================================================

def state_map_domain():
    """+1F0 every byte x +1F1 in {0, 1, 2, 3} x +236 in {0, 1} x +0D in
    {0, 1, 2, 3}; the routine reads nothing else (every other +1F1 / +236 /
    +0D value behaves as one of these, per its compares)."""
    out = []
    for st in range(256):
        for sub in (0, 1, 2, 3):
            for flag in (0, 1):
                for mode in (0, 1, 2, 3):
                    out.append((st, sub, flag, mode))
    return out


def state_map_check():
    ee = LeftEE(ELF, cover=True)
    s = NativeState(TABLE)
    count = 0
    for st, sub, flag, mode in state_map_domain():
        record = bytearray(0x320)
        record[0x1F0], record[0x1F1], record[0x236], record[0x0D] = st, sub, flag, mode
        put(record, 0x230, 4, 0xA5A5A5A5)
        ee.write(PLAYER, bytes(record))
        ee.r[29] = shared.STACK_TOP
        ee.call(ENTRY['state_map'], (PLAYER,))
        want = ee.read(PLAYER, 0x320)
        s.write('player', record)
        assert NATIVE.shim_run(CB(), 0, None, ENTRY_INDEX['state_map'], None, s.ptr('player'),
                               0, 0, 0, 0, None, None) == 0
        got = s.read('player')
        assert want == got, ('0015CBA0', st, sub, flag, mode, want[0x230:0x234].hex(), got[0x230:0x234].hex())
        count += 1
    return count, ee.outcomes


# ======================================================================
# Captured RAM: the original camera frame with the translations in place
# ======================================================================

VEC = 0x7F0E0000                 # private vectors for worker arguments (stack region)
DEPTH = [0]


class WorldBinding:
    """EmCamLeftWorkers bound to the ORIGINAL callees executed in the same EE
    on the same world; the native storage is synced into the EE around every
    call and back out after it."""

    def __init__(self, native, ee):
        self.ee, self.native = ee, native
        self.state = NativeState(TABLE)
        self.error = None
        s = self.state
        self.ids = (VP * 8)(s.ptr('cam'), s.ptr('player'), s.ptr('other'), s.ptr('eye'), s.ptr('target'),
                            C.addressof(s.hit), None, None)
        self.callback = CB(self.dispatch)
        self.workers = native.shim_workers()
        self.world = World(cam=s.ptr('cam'), player=s.ptr('player'), globals=C.addressof(s.globals),
                           scratch=s.ptr('scratch'), hit=C.addressof(s.hit), workers=self.workers, fault=0)

    SYNC = tuple(n for n in REGIONS if n != 'other')

    def sync_in(self):
        ee, s = self.ee, self.state
        for name in self.SYNC:
            base, size = REGIONS[name]
            s.write(name, ee.read(base, size))
        record = ee.load(HIT_POINTER)
        s.set_hit([ee.load(HIT_POINT + 4 * i) for i in range(4)], ee.load(record + 0x1A, 2),
                  [ee.load(record + 0x24 + 4 * i) for i in range(3)])

    def sync_out(self):
        ee, s = self.ee, self.state
        for name in self.SYNC:
            ee.write(REGIONS[name][0], s.read(name))

    def call(self, entry, args=(), fregs=()):
        self.sync_out()
        v0, f0 = nested_bits(self.ee, entry, args, fregs)
        self.sync_in()
        return s32(v0), f0 & MASK

    def put_vec(self, slot, values):
        for i, value in enumerate(values): self.ee.save(VEC + 0x40 * slot + 4 * i, value)
        return VEC + 0x40 * slot

    ADDRESS = {0: CAM, 1: PLAYER, 3: 0x8105D0, 4: 0x8105E0}

    def dispatch(self, wid, inp, nin, out):
        if self.error is not None:
            return -1
        try:
            name = WORKERS[wid]
            address = [a for a, n in CALLEES.items() if n == name][0]
            args = [inp[i] for i in range(nin)]
            if name in ('wrap', 'sine', 'cosine', 'sqrt'):
                out[0] = self.call(address, (), (args[0],))[1]
            elif name == 'approach':
                out[0] = self.call(address, (), (args[0], args[1], args[2]))[1]
            elif name == 'heading':
                out[0] = self.call(address, (self.put_vec(0, args[:3] + [0]),), (args[3], args[4]))[1]
            elif name == 'atan2':
                out[0] = self.call(address, (), (args[0], args[1]))[1]
            elif name == 'segment':
                out[0] = self.call(address, (self.put_vec(0, args[:4]), self.put_vec(1, args[4:8]), args[8]))[0] & MASK
            elif name == 'inside':
                out[0] = self.call(address, (args[0], self.put_vec(0, args[1:4] + [0]), args[4], args[5]))[0] & MASK
            elif name == 'solve_dispatch':
                out[0] = self.call(address, (self.ADDRESS[args[0]], args[1]))[0] & MASK
            elif name == 'w_001DD980':
                self.call(address, (self.ADDRESS[args[0]], self.ADDRESS[args[1]]))
            elif name in ('commit', 'w_0022EEF0') or name in HANDLERS or name == 'w_00193EB0':
                regs = [self.ADDRESS[args[0]]] + ([self.ADDRESS[args[1]]] if name in HANDLERS or
                                                  name == 'w_00193EB0' else [args[1]])
                if name == 'w_00193EB0':
                    regs.append(args[2])
                self.call(address, tuple(regs))
            else:
                self.call(address, tuple(args))
            return 0
        except BaseException as error:
            self.error = error
            return -1


def native_hook(native, kind, counts):
    def hook(ee):
        DEPTH[0] += 1
        try:
            native_call(native, kind, ee)
        finally:
            DEPTH[0] -= 1
        counts[kind] = counts.get(kind, 0) + 1
    return hook


def native_call(native, kind, ee):
    binding = WorldBinding(native, ee)
    binding.sync_in()
    s = binding.state
    a = ee.arg
    if kind not in ('height_5a0', 'state_map'):
        assert a(0) == CAM, (kind, 'camera record', hex(a(0)))
    if kind in ('dispatch', 'trigger', 'settle', 'settle_body', 'orient', 'tether', 'placement',
                'region', 'solve', 'bounds_d910', 'solve_aim', 'orbit'):
        assert a(1) == PLAYER, (kind, 'a1', hex(a(1)))
    vec = None
    a2 = s32(a(2))
    a3 = s32(a(3))
    if kind == 'height_5a0':
        assert a(0) == CAM + 0x10, (kind, hex(a(0)))
        vec = s.ptr('cam') + 0x10
    elif kind == 'bounds_ce60':
        if a(1) == PLAYER + 0xB0:
            vec = s.ptr('player') + 0xB0
        else:
            assert SCRATCH_BASE <= a(1) < SCRATCH_BASE + SCRATCH_SIZE, (kind, hex(a(1)))
            vec = s.ptr('scratch') + (a(1) - SCRATCH_BASE)
    v0 = C.c_int(-99)
    status = native.shim_run(binding.callback, 0, binding.ids, ENTRY_INDEX[kind], C.byref(binding.world),
                             s.ptr('player'), a2, a3, ee.f[12] & MASK, ee.f[13] & MASK, vec, C.byref(v0))
    if binding.error is not None:
        raise binding.error
    assert status == 0, (kind, 'native fault', status, hex(binding.world.fault))
    binding.sync_out()
    if kind in NativeRun.RETURNS:
        ee.ret_int(v0.value)


def counting_hook(address, kind, reached):
    def hook(ee):
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


CAPTURES = {}
FRAME_HOOKED = {address: name for address, (name, _) in MINE.items() if name != 'state_map'}


def captured_job(index):
    name, ram, spad = CAPTURES['sources'][index]
    results = []
    for native_mode in (False, True):
        ee = LeftEE(CAPTURES['elf'], ram, spad)
        counts = {}
        for address, kind in FRAME_HOOKED.items():
            ee.hooks[address] = (native_hook(CAPTURES['native'], kind, counts) if native_mode
                                 else counting_hook(address, kind, counts))
        ee.call(0x18B9C0, (CAM,))
        results.append((follow.frame_digest(ee), counts, ee.read(CAM, 0xD0), ee.read(0x8105D0, 0x40)))
    return name, results


# Further captured images whose camera frame takes the other state-1 arms:
# the opening hand-off (+4 = 0 with action 8: 001914A0, the mode-8 settle;
# no scratchpad was captured with it, so a zeroed one stands in and the
# comparison is original against translated from the same bytes), the
# opening and Roger's encounter (+4 = 3), the panel and status-hub scenes
# (+4 = 1).
EXTRA_CAPTURES = (('handoff', 'handoff_ee.bin', None), ('opening', 'opening_ee.bin', 'opening_scratchpad.bin'),
                  ('panel', 'panel/animation_ee.bin', 'panel/animation_scratchpad.bin'),
                  ('roger-encounter', 'roger-encounter/eeMemory.bin', 'roger-encounter/scratchpad.bin'),
                  ('status-hub', 'status-hub/eeMemory.bin', 'status-hub/scratchpad.bin'))


def extra_sources():
    out = []
    for name, ram, spad in EXTRA_CAPTURES:
        ram_path = shared.REFERENCE / ram
        spad_path = shared.REFERENCE / spad if spad else None
        if not ram_path.exists() or (spad_path is not None and not spad_path.exists()):
            continue
        out.append((name, ram_path.read_bytes(), spad_path.read_bytes() if spad_path else bytes(0x4000)))
    return out


def captured_main(elf, native):
    sources = follow.captured_sources() + extra_sources()
    if not sources:
        return 'captured check skipped (no captured RAM; docs/CAMERA_LEFTOVERS.md)'
    CAPTURES.update(elf=elf, native=native, sources=sources)
    results = reference_mode.parallel_map(captured_job, range(len(sources)))
    total, entered = {}, {}
    for name, ((a_hash, reached, a_cam, a_eye), (b_hash, counts, b_cam, b_eye)) in results:
        if a_cam != b_cam:
            raise AssertionError((name, 'camera record differs at',
                                  [hex(k) for k in range(0xD0) if a_cam[k] != b_cam[k]]))
        assert a_eye == b_eye, (name, 'camera globals differ')
        assert a_hash == b_hash, (name, 'RAM or scratchpad differs outside the camera record')
        # the translated run enters the native module from original code only
        # (a translation calls the other translations directly)
        assert set(counts) <= set(reached), (name, 'a translation ran where the original did not',
                                             reached, counts)
        for kind, n in reached.items():
            total[kind] = total.get(kind, 0) + n
        for kind, n in counts.items():
            entered[kind] = entered.get(kind, 0) + n
    assert total.get('solve', 0) > 0, ('no captured image reaches 0018DD20', total)
    return ('captured: %d images through 0018B9C0 identical (whole RAM + scratchpad); the original '
            'reached %s; native entries from original code %s' % (
                len(sources), ', '.join('%s %d' % kv for kv in sorted(total.items())),
                ', '.join('%s %d' % kv for kv in sorted(entered.items()))))


# ======================================================================
# World mode: the route beats replayed with the camera frame every frame
# ======================================================================

WORLD = {}


LeftRoute = follow.CameraRoute    # the player stage as the follow test replays it


def state_map_hook(native, tally):
    """0015CBA0 inside the original player stage: the original runs, and
    the translation, run on a copy of the record as it was, must leave the
    same 0x320 bytes."""
    def hook(ee):
        record = ee.arg(0)
        before = ee.read(record, 0x320)
        saved = ee.hooks.pop(0x15CBA0)
        try:
            nested_bits(ee, 0x15CBA0, (record,))
        finally:
            ee.hooks[0x15CBA0] = saved
        actor = LiveActor()
        C.memmove(actor.bytes, before, 0x320)
        assert native.shim_run(CB(), 0, None, ENTRY_INDEX['state_map'], None, C.addressof(actor),
                               0, 0, 0, 0, None, None) == 0
        assert bytes(actor.bytes) == ee.read(record, 0x320), ('0015CBA0 differs on the route', hex(record))
        tally[before[0x1F0]] = tally.get(before[0x1F0], 0) + 1
    return hook


def beat_replay(job):
    beat, last = job
    trace, ram, spad = WORLD[beat]
    replay = LeftRoute(WORLD['elf'], trace, ram, spad)
    ee = replay.ee
    mapped = {}
    ee.hooks[0x15CBA0] = state_map_hook(WORLD['native'], mapped)
    counts, reached = {}, {}
    original = {address: counting_hook(address, kind, reached) for address, kind in FRAME_HOOKED.items()}
    native = {address: native_hook(WORLD['native'], kind, counts) for address, kind in FRAME_HOOKED.items()}
    frames, errors = 0, []
    end = trace['first_counter'] + last
    while replay.counter < end:
        row = replay.step()
        mem, spad_before = bytearray(ee.mem), bytearray(ee.spad)
        ee.hooks.update(native)
        ee.call(0x18B9C0, (CAM,))
        translated = follow.frame_digest(ee)
        ee.mem[:], ee.spad[:] = mem, spad_before
        ee.hooks.update(original)
        ee.call(0x18B9C0, (CAM,))
        assert follow.frame_digest(ee) == translated, (beat, replay.counter, 'RAM or scratchpad differs')
        frames += 1
        if row is not None and row['cam_mode'][:2] == '00':
            eye = [number(ee.load(0x8105D0 + 4 * i)) for i in range(3)]
            errors.append(max(abs(eye[i] - row['eye'][i]) for i in range(3)))
    return frames, counts, reached, errors, mapped


def world_main():
    global TABLE
    elf = read_elf()
    TABLE = table_words(elf)
    beats = [b for b in os.environ.get('EM_WORLD_BEATS', ','.join(follow.WORLD_BEATS)).split(',') if b]
    for beat in beats:
        loaded = shared.route_beat(beat)
        if isinstance(loaded, str):
            raise SystemExit('world mode: %s (docs/CAMERA_LEFTOVERS.md)' % loaded)
        WORLD[beat] = loaded
    WORLD['elf'], WORLD['native'] = elf, build_native()
    cap = int(os.environ.get('EM_WORLD_FRAMES', '0') or 0)
    jobs = []
    for beat in beats:
        trace = WORLD[beat][0]
        length = follow.WORLD_BEATS.get(beat) or max(r['counter'] for r in trace['rows']) - trace['first_counter']
        jobs.append((beat, min(length, cap) if cap else length))
    started = time.time()
    results = reference_mode.parallel_map(beat_replay, jobs, cost=lambda job: job[1])
    for (beat, _), (frames, counts, reached, errors, mapped) in zip(jobs, results):
        assert set(counts) <= set(reached), (beat, 'a translation ran where the original did not',
                                             reached, counts)
        assert reached.get('solve', 0) > 0, (beat, 'the replay never reached 0018DD20', reached)
        errors.sort()
        spread = ('actual eye vs trace rows in follow mode: median %.3g, max %.3g over %d rows' %
                  (errors[len(errors) // 2], errors[-1], len(errors))) if errors else 'no follow rows'
        assert sum(mapped.values()) > 0, (beat, 'the player stage never ran 0015CBA0')
        print('%s: PASS %d frames identical (whole RAM + scratchpad); the original reached %s; '
              '0015CBA0 identical on %d player-stage calls (+1F0 %s); %s' % (
                  beat, frames, ', '.join('%s %d' % kv for kv in sorted(reached.items())),
                  sum(mapped.values()), ' '.join('%d:%d' % kv for kv in sorted(mapped.items())), spread))
    print('camera leftovers world mode: PASS (%.0fs)' % (time.time() - started))


# ======================================================================
# Main
# ======================================================================

def table_words(elf):
    ee = EE(elf)
    return [ee.load(TABLE_24A5F0 + 4 * i) for i in range(TABLE_WORDS)]


def main():
    global ELF, NATIVE, TABLE
    started = time.time()
    ELF = read_elf()
    TABLE = table_words(ELF)
    callees = check_callee_set(ELF)
    NATIVE = build_native()
    total = reference_mode.pick(80000, 80000)
    seeds = reference_mode.select(range(total), 8000, 0xCA7E)
    results = reference_mode.parallel_map(run_case, seeds)
    outcomes, entries, faults, calls, reached = set(), {}, 0, 0, set()
    for entry, count, fault, cover, called in results:
        outcomes.update(cover)
        entries[entry] = entries.get(entry, 0) + 1
        faults += fault
        calls += count
        reached.update(called)
    mapped, map_cover = state_map_check()
    outcomes.update(map_cover)
    sites = branch_sites(ELF)
    impossible = sorted(o for o in INFEASIBLE if o in outcomes)
    assert not impossible, ('an outcome recorded as unreachable occurred', impossible)
    missing = sorted('%06X %s' % (pc, 'taken' if taken else 'not taken')
                     for pc in sites for taken in (True, False)
                     if (pc, taken) not in outcomes and (pc, taken) not in INFEASIBLE)
    assert not missing, ('branch outcomes never exercised', len(missing), missing)
    assert reached == set(WORKERS), ('workers never called', sorted(set(WORKERS) - reached))
    refusal_seeds = reference_mode.select(range(0, total, 7), 60, 0x5EED)
    refusals = sum(reference_mode.parallel_map(refusal_job, refusal_seeds))
    captured = captured_main(ELF, NATIVE)
    reference_mode.banner(reference_mode.part(len(seeds), total, 'cases'),
                          '%d jal targets (all hooked or translated)' % callees)
    print('camera leftovers vs original instructions: PASS %d cases (%s), %d worker calls identical, '
          'every one of %d conditional branches both ways (%d unreachable outcomes never seen), %d fault-stop cuts, %d refusals over %d '
          'cases, 0015CBA0 %d inputs; %s (%.1fs)' % (
              len(seeds), ', '.join('%s %d' % kv for kv in sorted(entries.items())), calls,
              len(sites), len(INFEASIBLE), faults, refusals, len(refusal_seeds), mapped, captured, time.time() - started))


if __name__ == '__main__':
    if os.environ.get('EM_TEST_WORLD', '') not in ('', '0'):
        world_main()
    else:
        main()
