#!/usr/bin/env python3
"""Execute the original lane "script-door-fan" functions and compare the
native translations (docs/SCRIPT_DOOR_FAN.md).

Part 1, script-host handlers (census L19 rows marked unverified). The
em_area_script lockstep of tools/test_area_script_reference.py is rerun on
the scenarios that reach 001B6BF0, 001B6E40, 001B6F80, 001B6FA0, 001B7840,
001B81D0 and 001BA080. This test records which original instructions of each
handler executed (the handlers run as original instructions inside the
original 001BA1F0; none is hooked) while every lockstep comparison of that
test passes, and asserts that each handler's entry ran.

Part 2, em_script_door_fan.c: 001BA510, 001BAC00, 001BAD40, 001B1B30,
001BC240, 001BC290 and 001BBD60 execute as original instructions from the
user's pinned ELF over the captured first-control RAM; every callee is a
hook recorded with its arguments and answered from one per-case script that
also answers the native workers. Compared: the result, the ordered calls,
every modelled record/global byte, and every original write must fall inside
the modelled bytes. The opening capture checks the fields 001BAC00/001BAD40
leave on the two actors the opening spawns.

Part 3, em_script_door_fan_husk.c: the AREA11 overlay functions 0x825940,
0x827490 and 0x823CE0 execute from the captured RAM (the test first asserts
those bytes equal the user's extract/OVERLAY/AREA11.BIN at the same offsets).
Every captured AREA11 RAM image is ticked from its captured state; unit
cases and multi-tick lockstep runs cover the rest. The creature's lifecycles
1 and 4 are not translated: the test asserts the original enters them at
0x826190 / 0x825B74 exactly where the native faults.

Branch coverage: every conditional branch inside the translated ranges must
be observed both ways; exceptions are listed in BRANCH_EXCEPTIONS with the
reason.

Arithmetic: COP1 goes through tools/ee_float_model.py (FallEE of
test_player_fall_reference); the native side uses em_ee_float.h.

Quick mode (default) samples the bulk sweeps and uses four RAM images; every
case class, boundary and multi-tick run is kept. EM_TEST_FULL=1 runs every
sweep value and every capture. No original instruction bytes, disassembly or
game data are written by this file.
"""
import ctypes as C
import hashlib
import itertools
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from reference_mode import FULL, banner, parallel_map, pick, select  # noqa: E402
import test_player_slide_reference as shared  # noqa: E402
from test_player_fall_reference import FallEE, nested_bits  # noqa: E402
from test_player_slide_reference import bits, number, sx32  # noqa: E402

MASK = 0xFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
REF = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'
AREA11_BIN = DECOMP / 'extract/OVERLAY/AREA11.BIN'
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
OUT = ROOT / 'build' / 'script_door_fan_reference'
ARENA = 0x823500
PLAYER = 0x8102B0

# Scratch addresses for test-generated records (not game data).
OWNER, REC, LIST, NODES = 0x01D10000, 0x01D10400, 0x01D10800, 0x01D20000
ACT, ENT, TRACKS, HANDLES = 0x01D30000, 0x01D30400, 0x01D31000, 0x00E00000
CHILD, LINKED, SLOTS, BONE, CHILD_BONE, SELF_BONE = (0x01D40000, 0x01D40400, 0x01D40800,
                                                     0x01D40C00, 0x01D41000, 0x01D41400)
DRAW = 0x01F80000                   # sentinel +0x4C draw callback address

# Translated original ranges [start, end).
RANGES = {
    '001BA510': (0x1BA510, 0x1BA540), '001BAC00': (0x1BAC00, 0x1BAD3C),
    '001BAD40': (0x1BAD40, 0x1BB0E0), '001B1B30': (0x1B1B30, 0x1B1B70),
    '001BC240': (0x1BC240, 0x1BC284), '001BC290': (0x1BC290, 0x1BC300),
    '001BBD60': (0x1BBD60, 0x1BBD94), '001B0080': (0x1B0080, 0x1B0244),
    'creature': (0x825940, 0x825B74), 'creature-2': (0x826D60, 0x826F2C),
    'partner': (0x827490, 0x827628), 'manager': (0x823CE0, 0x823E80),
}
UNTRANSLATED = {1: 0x826190, 4: 0x825B74}   # creature lifecycles (fault)
# Conditional branches that cannot go both ways, with the reason.
BRANCH_EXCEPTIONS = {}


def s16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v
def s32(v): v &= MASK; return v - 0x100000000 if v & 0x80000000 else v


# ======================================================================
# Oracle: FallEE over a shared captured RAM image with copy-on-write bytes
# ======================================================================

class Oracle(FallEE):
    def __init__(self, base, spad=None):   # noqa: super().__init__ copies 32 MB; not needed
        self.base = base
        self.over = {}
        self.spad = bytearray(spad) if spad is not None else bytearray(0x4000)
        self.stack = bytearray(0x100000)
        self.r = [0] * 32; self.rh = [0] * 32; self.hi = self.lo = 0
        self.f = [0] * 32; self.acc = 0; self.cond = False
        self.vf = [[0, 0, 0, 0] for _ in range(32)]; self.vf[0][3] = bits(1.0)
        self.vacc = [0, 0, 0, 0]; self.q = 0
        self.hooks = {}; self.log = []; self.steps = 0; self.limit = 2_000_000
        self.r[28] = 0x27D370; self.r[29] = shared.STACK_TOP
        self.written = set(); self.outcomes = set(); self.fetched = set()

    def load(self, address, size=4):
        a = address & MASK
        if 0x70000000 <= a < 0x70004000:
            return int.from_bytes(self.spad[a - 0x70000000:a - 0x70000000 + size], 'little')
        if 0x7F000000 <= a < 0x7F100000:
            return int.from_bytes(self.stack[a - 0x7F000000:a - 0x7F000000 + size], 'little')
        assert a + size <= 0x2000000, ('load outside RAM', hex(a))
        value = 0
        for i in range(size):
            b = self.over.get(a + i)
            value |= (self.base[a + i] if b is None else b) << (8 * i)
        return value

    def save(self, address, value, size=4, track=True):
        a = address & MASK
        if 0x7F000000 <= a < 0x7F100000:
            self.stack[a - 0x7F000000:a - 0x7F000000 + size] = (
                value & ((1 << 8 * size) - 1)).to_bytes(size, 'little')
            return
        for i in range(size):
            byte = value >> (8 * i) & 255
            if 0x70000000 <= a + i < 0x70004000: self.spad[a + i - 0x70000000] = byte
            else:
                assert a + i < 0x2000000, ('store outside RAM', hex(a))
                self.over[a + i] = byte
            if track: self.written.add(a + i)

    def seed(self, address, value, size=4):
        self.save(address, value, size, track=False)

    def seed_bytes(self, address, data):
        for i, b in enumerate(data): self.save(address + i, b, 1, track=False)

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None:
            self.outcomes.add((pc, b[0]))
        self.fetched.add(pc)
        return b

    def f32(self, address): return self.load(address)


def run_original(o, entry, args=(), fregs=()):
    for i, value in enumerate(args): o.r[4 + i] = sx32(value)
    for i, value in enumerate(fregs): o.f[12 + i] = value & MASK
    o.r[31] = shared.RETURN
    o.run(entry)
    return o.r[2] & MASK


def hook(o, address, fn):
    o.hooks[address] = fn


# ======================================================================
# Native library and ctypes mirrors
# ======================================================================

P8, P16, PU16, P32, PI32, PF = (C.POINTER(C.c_uint8), C.POINTER(C.c_int16), C.POINTER(C.c_uint16),
                                C.POINTER(C.c_uint32), C.POINTER(C.c_int32), C.POINTER(C.c_float))


class Fault(C.Structure):
    _fields_ = [('address', C.c_uint32), ('code', C.c_int32)]


class Image(C.Structure):
    _fields_ = [('bytes', C.POINTER(C.c_ubyte)), ('base', C.c_uint32), ('length', C.c_uint32)]


class Spawned(C.Structure):
    _fields_ = [('b03', C.c_uint8), ('b0D', C.c_uint8), ('handler_10', C.c_uint32),
                ('entry_20', C.c_uint32), ('owner_24', C.c_uint32), ('s2E', C.c_int16),
                ('pos_B0', C.c_float * 3), ('rot_C0', C.c_float * 3)]


class SpawnOwner(C.Structure):
    _fields_ = [('self_14', C.c_uint32), ('s2E', C.c_int16)]


class EventActor(C.Structure):
    _fields_ = [('lifecycle', C.c_uint8), ('b09', C.c_uint8), ('b0C', C.c_uint8),
                ('w18', C.c_uint32), ('bank_40', C.c_uint32), ('w44', C.c_uint32),
                ('bones_110', C.c_uint32 * 0x78)]


class World(C.Structure):
    _fields_ = [('d2821B0', PI32), ('d2821B4', PI32), ('d2821B8', PI32), ('d8106C0', P32),
                ('d810250', P32), ('d810254', PF), ('d810258', PF), ('d8101E4', P8),
                ('d81024E', P16), ('d275BCC', P16), ('d8106B8', P8)]


class DoorStep(C.Structure):
    _fields_ = [('b0B', C.c_uint8), ('anim_flags', C.c_int16)]


class Seat(C.Structure):
    _fields_ = [('f0C', C.c_float), ('eye_10', C.c_float * 4), ('tgt_20', C.c_float * 4),
                ('rot_30', C.c_float * 4)]


class SeatWorld(C.Structure):
    _fields_ = [('d810700', P8), ('d810702', P8), ('d810350', PF), ('spad3B50', PF),
                ('d8105D0', PF), ('d8105E0', PF), ('spad3400', P32), ('spad3600', P32)]


PSP = C.POINTER(C.POINTER(Spawned))
PEA = C.POINTER(EventActor)
F = C.CFUNCTYPE
SDF_WORKERS = [
    ('r_0028A490', F(C.c_int, C.c_void_p, C.c_int32, P32)),
    ('r_track_head', F(C.c_int, C.c_void_p, C.c_uint32, PF)),
    ('r_0024DB80', F(C.c_int, C.c_void_p, C.c_uint32, PU16)),
    ('w_001AFA90', F(C.c_int, C.c_void_p, C.c_uint8, P32, PSP)),
    ('w_001C8140', F(C.c_int, C.c_void_p, C.c_uint32, C.c_int16, C.c_uint32, P32, PSP)),
    ('w_001CA6E0', F(C.c_int, C.c_void_p, PEA, C.c_uint32)),
    ('w_001C6120', F(C.c_int, C.c_void_p, C.c_uint32, C.c_int32, P32)),
    ('w_0022EC30', F(C.c_int, C.c_void_p, C.c_uint32)),
    ('w_001C5C90', F(C.c_int, C.c_void_p, PEA)),
    ('w_001C6150', F(C.c_int, C.c_void_p, C.c_uint32, PI32)),
    ('w_001AF780', F(C.c_int, C.c_void_p, P32)),
    ('w_001BA8E0', F(C.c_int, C.c_void_p, PEA, C.c_int16)),
    ('w_001D8BF0', F(C.c_int, C.c_void_p, PEA, C.c_int32)),
    ('w_001CA6F0', F(C.c_int, C.c_void_p, PEA, C.c_uint8)),
    ('w_001CB5B0', F(C.c_int, C.c_void_p, C.c_uint8)),
    ('w_001C63E0', F(C.c_int, C.c_void_p, PEA, C.c_int16)),
    ('w_001C61D0', F(C.c_int, C.c_void_p, C.c_uint32, C.c_int16, PI32)),
    ('w_001C67E0', F(C.c_int, C.c_void_p, C.c_int16, C.c_float, C.c_float)),
    ('w_001B1630', F(C.c_int, C.c_void_p, C.c_float, C.c_float, C.c_float, PI32)),
    ('w_001B1B70', F(C.c_int, C.c_void_p)),
    ('w_001C64F0', F(C.c_int, C.c_void_p, C.c_float, P16)),
    ('w_001BC150', F(C.c_int, C.c_void_p)),
    ('w_001B1470', F(C.c_int, C.c_void_p, C.c_float, PF)),
    ('w_001029C0', F(C.c_int, C.c_void_p, P32)),
    ('w_00102C58', F(C.c_int, C.c_void_p, P32, P32, PF)),
    ('w_001026A0', F(C.c_int, C.c_void_p, PF, P32, P32)),
]


class SdfWorkers(C.Structure):
    _fields_ = [('ctx', C.c_void_p)] + [(n, t) for n, t in SDF_WORKERS]


class Child(C.Structure):
    _fields_ = [('b03', C.c_uint8), ('b0D', C.c_uint8), ('h0E', C.c_uint16),
                ('handler_10', C.c_uint32), ('h2E', C.c_uint16), ('h54', C.c_uint16),
                ('h56', C.c_uint16), ('b9A', C.c_uint8), ('fA0', C.c_float * 4),
                ('pos_B0', C.c_uint32 * 4), ('rot_C0', C.c_uint32 * 4), ('bone3_11C', C.c_uint32)]


class Creature(C.Structure):
    _fields_ = [('b00', C.c_uint8), ('lifecycle', C.c_uint8), ('timer_28', C.c_int16),
                ('pos_B0', C.c_uint32 * 4), ('rot_C0', C.c_uint32 * 4), ('bone3_11C', C.c_uint32),
                ('f1F4', C.c_float), ('f1F8', C.c_float), ('f1FC', C.c_float),
                ('w200', C.c_uint32), ('w204', C.c_uint32), ('w208', C.c_uint32),
                ('w20C', C.c_uint32), ('f210', C.c_float), ('f214', C.c_float),
                ('f218', C.c_float), ('w21C', C.c_int32), ('child_220', C.c_uint32),
                ('w224', C.c_int32), ('freed', C.c_uint8)]


class Partner(C.Structure):
    _fields_ = [('b00', C.c_uint8), ('lifecycle', C.c_uint8), ('timer_28', C.c_int16),
                ('h34', C.c_int16), ('hit_36', C.c_int16), ('item_9A', C.c_uint8),
                ('freed', C.c_uint8)]


class Linked(C.Structure):
    _fields_ = [('lifecycle', C.c_uint8), ('w21C', C.c_int32)]


class Manager(C.Structure):
    _fields_ = [('b00', C.c_uint8), ('lifecycle', C.c_uint8), ('phase', C.c_uint8),
                ('h28', C.c_int16), ('h2E', C.c_uint16), ('freed', C.c_uint8)]


class HuskWorld(C.Structure):
    _fields_ = [('d810758', P8), ('d8106C8', P32), ('d810808', P8), ('spad3A20', PF)]


PPC = C.POINTER(C.POINTER(Child))
V = C.c_void_p
HUSK_WORKERS = [
    ('w_001C6380', F(C.c_int, V)), ('w_001B17A0', F(C.c_int, V)), ('w_draw_4C', F(C.c_int, V)),
    ('w_001AFC10', F(C.c_int, V)), ('w_001B0FD0', F(C.c_int, V, PI32)),
    ('w_00122BB8', F(C.c_int, V, PI32)), ('w_0011E2A8', F(C.c_int, V, C.c_float, PF)),
    ('r_00275B40', F(C.c_int, V, C.c_uint32, P32)),
    ('s_bone_f32', F(C.c_int, V, C.c_uint32, C.c_uint32, C.c_float)),
    ('w_001A2370', F(C.c_int, V, C.c_uint32)),
    ('w_001AFA90', F(C.c_int, V, C.c_uint8, P32, PPC)),
    ('r_child_220', F(C.c_int, V, C.c_uint32, PPC)),
    ('w_00102958', F(C.c_int, V, C.c_uint32, C.c_uint32)),
    ('w_001B11E0', F(C.c_int, V, C.c_uint8, PI32)), ('w_001B1190', F(C.c_int, V, C.c_uint8)),
    ('w_001EFE00', F(C.c_int, V, C.c_uint32, PI32)),
    ('w_001FBD50', F(C.c_int, V, C.c_int32, C.c_int32, C.c_float)),
    ('r_link_18', F(C.c_int, V, C.POINTER(C.POINTER(Linked)))),
    ('w_001BA1A0', F(C.c_int, V, C.c_uint32)), ('w_001BA1F0', F(C.c_int, V, PI32)),
    ('w_001D2830', F(C.c_int, V, C.c_int32, C.c_int32)), ('w_001C1DC0', F(C.c_int, V)),
    ('w_001FABB0', F(C.c_int, V)), ('w_001AEE10', F(C.c_int, V, C.c_int32, C.c_int32)),
    ('w_001C4760', F(C.c_int, V, C.c_int32, C.c_int32)), ('w_001FAE70', F(C.c_int, V, C.c_int32)),
]


class HuskWorkers(C.Structure):
    _fields_ = [('ctx', C.c_void_p)] + [(n, t) for n, t in HUSK_WORKERS]


LAYOUT = r'''
#include <stddef.h>
#include <stdio.h>
#include "game/em_script_door_fan.h"
#include "game/em_script_door_fan_husk.h"
#define S(t) printf("%zu ", sizeof(t))
#define O(t, f) printf("%zu ", offsetof(t, f))
int main(void) {
  S(EmSdfSpawned); O(EmSdfSpawned, s2E); O(EmSdfSpawned, rot_C0);
  S(EmSdfSpawnOwner); S(EmSdfEventActor); O(EmSdfEventActor, bones_110);
  S(EmSdfWorld); O(EmSdfWorld, d8106B8); S(EmSdfDoorStep); S(EmSdfImage);
  S(EmSdfWorkers); O(EmSdfWorkers, w_001BC150); S(EmSdfSeat); S(EmSdfSeatWorld);
  S(EmHuskChild); O(EmHuskChild, bone3_11C); S(EmHuskCreature); O(EmHuskCreature, freed);
  S(EmHuskPartner); S(EmHuskLinked); S(EmHuskManager); S(EmHuskWorld);
  S(EmHuskWorkers); O(EmHuskWorkers, w_001FAE70);
  printf("\n"); return 0; }
'''


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / 'script_door_fan.dylib'
    sources = ['src/game/em_script_door_fan.c', 'src/game/em_script_door_fan_husk.c']
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', *sources, '-o', str(lib)],
                   cwd=ROOT, check=True)
    probe = OUT / 'layout.c'
    probe.write_text(LAYOUT)
    subprocess.run(['cc', '-std=c11', '-Isrc', str(probe), '-o', str(OUT / 'layout')], cwd=ROOT,
                   check=True)
    got = [int(x) for x in subprocess.run([str(OUT / 'layout')], capture_output=True, text=True,
                                          check=True).stdout.split()]
    mine = [C.sizeof(Spawned), Spawned.s2E.offset, Spawned.rot_C0.offset, C.sizeof(SpawnOwner),
            C.sizeof(EventActor), EventActor.bones_110.offset, C.sizeof(World),
            World.d8106B8.offset, C.sizeof(DoorStep), C.sizeof(Image), C.sizeof(SdfWorkers),
            SdfWorkers.w_001BC150.offset, C.sizeof(Seat), C.sizeof(SeatWorld), C.sizeof(Child), Child.bone3_11C.offset,
            C.sizeof(Creature), Creature.freed.offset, C.sizeof(Partner), C.sizeof(Linked),
            C.sizeof(Manager), C.sizeof(HuskWorld), C.sizeof(HuskWorkers),
            HuskWorkers.w_001FAE70.offset]
    assert got == mine, ('ctypes layout differs from C', got, mine)
    native = C.CDLL(str(lib))
    native.em_sdf_001BA510.argtypes = [P8, C.POINTER(Fault)]
    native.em_sdf_001BAC00.argtypes = [C.POINTER(SpawnOwner), C.POINTER(C.c_ubyte),
                                       C.POINTER(Image), C.POINTER(SdfWorkers), C.POINTER(Fault)]
    native.em_sdf_001BAD40.argtypes = [PEA, C.POINTER(C.c_ubyte), C.POINTER(World),
                                       C.POINTER(SdfWorkers), C.POINTER(Fault)]
    native.em_sdf_001B1B30.argtypes = [P8, C.c_float, C.c_float, C.c_float,
                                       C.POINTER(SdfWorkers), C.POINTER(Fault)]
    native.em_sdf_001BC240.argtypes = [C.POINTER(DoorStep), C.POINTER(SdfWorkers), C.POINTER(Fault)]
    native.em_sdf_001BC290.argtypes = [C.POINTER(DoorStep), C.POINTER(World),
                                       C.POINTER(SdfWorkers), C.POINTER(Fault)]
    native.em_sdf_001B0080.argtypes = [C.POINTER(Seat), C.c_float, C.POINTER(SeatWorld),
                                       C.POINTER(SdfWorkers), C.POINTER(Fault)]
    native.em_sdf_001BBD60.argtypes = [C.c_int16, C.c_uint16, P32, C.POINTER(SdfWorkers),
                                       C.POINTER(Fault)]
    native.em_husk_creature_tick.argtypes = [C.POINTER(Creature), C.POINTER(HuskWorld),
                                             C.POINTER(HuskWorkers), C.POINTER(Fault)]
    native.em_husk_partner_tick.argtypes = [C.POINTER(Partner), C.POINTER(HuskWorkers),
                                            C.POINTER(Fault)]
    native.em_husk_manager_tick.argtypes = [C.POINTER(Manager), C.POINTER(HuskWorld),
                                            C.POINTER(HuskWorkers), C.POINTER(Fault)]
    return native


def fnum(b):
    """A float argument for ctypes from raw bits (bit-exact for finite values)."""
    return number(b & MASK)


def fb(value):
    """Bits of a ctypes float value."""
    return struct.unpack('<I', struct.pack('<f', value))[0]


CONTEXT = {}


def ram(name='playable'):
    return CONTEXT['images'][name]


# ======================================================================
# Part 1: script-host handler coverage through the em_area_script lockstep
# ======================================================================

HANDLERS = {0x1B6BF0: 384, 0x1B6E40: 88, 0x1B6F80: 24, 0x1B6FA0: 1020, 0x1B7840: 488,
            0x1B81D0: 248, 0x1BA080: 276}
HANDLER_SCENARIOS = ('roger 8283D0', 'roger 8283D0 skip@14', 'roger 828990', 'roger 828810',
                     'roger 828A10', 'director 8294C0', 'director 8294C0 skip@8',
                     'synthetic flags+euler', 'synthetic fades', 'synthetic skippable',
                     'synthetic skippable skip@5', 'synthetic enter 7 face')
COVERAGE = set()
# Quick mode: a covering subset (every instruction the full set executes is
# executed by these; chosen once by a greedy cover weighted by run time).
QUICK_COVER = (
    'synthetic skippable skip@5', 'face model 3E', 'face model 40', 'face model 55',
    'face model 3F', 'face attach refused', 'op10 sub4 waits', 'op06 waits and high subs',
    'synthetic fades', 'synthetic flags+euler', 'roger 8283D0', 'roger 8283D0 skip@14',
    'op16 frame busy', 'op15 side 60.0 yaw -1.0 rates (0.3, 0.0) early 0 anim 0x0',
    'op15 side 60.0 yaw 2.5 rates (0.0, 0.2) early 0 anim 0x0',
    'op15 side -60.0 yaw 2.5 rates (0.0, 0.0) early 1 anim 0x0',
    'op15 side 60.0 yaw -1.0 rates (0.1, 0.1) early 0 anim 0x1000')


def extra_scripts(asr):
    """Test-generated records (not game data) for handler paths the level
    scripts do not reach, appended to test_area_script_reference's synthetic
    arena. Returns [(label, entry, scenario)] and the arena bytes."""
    base_scripts, data = asr.synthetic_arena()
    data = bytearray(data)
    rec, END, S = asr.rec, asr.END, asr.SPAD_IDLE
    out = []

    def script(label, records, **scenario):
        entry = asr.SYNTHETIC[0] + len(data)
        for r in records: data.extend(r)
        scenario.setdefault('init', S)
        scenario['keep_durations'] = 1
        out.append((label, entry, scenario))
    # op16 while a scripted frame is open: waits until 3B8D clears.
    script('op16 frame busy', [rec(0x16), rec(7, 4, END)],
           init=S + [(0x70003B8D, 1, 1)], set={3: [(0x70003B8D, 0, 1)]})
    # op06 waits (sub2 counter zero, sub4 mismatch) and subs 7+ (no-op).
    script('op06 waits and high subs', [
        rec(6, 2, w14=0x50), rec(6, 4, w14=0x51, w18=3), rec(6, 7, w14=0x52),
        rec(6, 0x10, w14=0x53), rec(7, 4, END)],
        init=S + [(0x8107D8 + 0x50, 0, 1), (0x8107D8 + 0x51, 0, 1)],
        set={3: [(0x8107D8 + 0x50, 1, 1)], 6: [(0x8107D8 + 0x51, 3, 1)]})
    # op10 sub 10 and above (no-op); sub4 waiting on a non-zero substate.
    script('op10 high subs', [rec(0x10, 10), rec(0x10, 0x40), rec(7, 4, END)])
    script('op10 sub4 waits', [rec(0x10, 4), rec(7, 4, END)],
           init=S + [(0x28A9A0, 1, 2)], set={3: [(0x28A9A0, 0, 2)]})
    # 001B81D0 through op07 sub7: every face selector and a refused attach.
    for label, patches, ca700 in (('78F set', [(0x81078F, 1, 1)], 1),
                                  ('model 3E', [(0x81078F, 0, 1), (PLAYER + 0x2FF, 0x3E, 1)], 1),
                                  ('model 3F', [(0x81078F, 0, 1), (PLAYER + 0x2FF, 0x3F, 1)], 1),
                                  ('model 40', [(0x81078F, 0, 1), (PLAYER + 0x2FF, 0x40, 1)], 1),
                                  ('model 55', [(0x81078F, 0, 1), (PLAYER + 0x2FF, 0x55, 1)], 1),
                                  ('attach refused', [(0x81078F, 1, 1)], 0)):
        script(f'face {label}', [rec(7, 7), rec(2, f0c=2.), rec(7, 4, END)],
               init=S + patches, ca700=ca700)
    # op15: owner on either side of the player, zero and non-zero turn
    # rates, the message slot freed early, the animation bit set early.
    px, py, pz = (struct.unpack('<f', CONTEXT['asr_ram'][PLAYER + 0xA0 + 4 * i:
                                                       PLAYER + 0xA4 + 4 * i])[0]
                  for i in range(3))
    for side, yaw, rates, early, anim in itertools.product(
            (60.0, -60.0), (2.5, -1.0), ((0., 0.), (0.1, 0.1), (0., 0.2), (0.3, 0.)), (0, 1),
            (0, 0x1000)):
        if early and rates != (0., 0.) or anim and rates != (0.1, 0.1): continue
        owner = [(asr.SYNTHETIC_OWNER + 0xB0, bits(px + side), 4),
                 (asr.SYNTHETIC_OWNER + 0xB4, bits(py), 4),
                 (asr.SYNTHETIC_OWNER + 0xB8, bits(pz + 30.0), 4),
                 (asr.SYNTHETIC_OWNER + 0xC4, bits(yaw), 4),
                 (PLAYER + 0x200, anim, 4)]
        extra = dict(set={4: [(0x2821B0, 0, 4)]}) if early else {}
        script(f'op15 side {side} yaw {yaw} rates {rates} early {early} anim {anim:#x}', [
            rec(0x15, 0x40, jump=0x21, w14=0x22, w18=0x23, w1c=0x24,
                v20=(0., rates[0], 0., 0.), v30=(0., rates[1], 0., 0.)), rec(7, 4, END)],
            init=S + owner, anim_ticks=40, message_ticks=12, **extra)
    return out, bytes(data)


# Original instructions of the seven handlers that no case executes, each
# with the reason (addresses only). Dead: follows an unconditional branch's
# delay slot and is no branch target. Unreachable: needs a state the
# original never produces.
UNEXECUTED = {
    0x1B6C20: 'unreachable: 001B6BF0 phase >= 2 (it writes phases 0 and 1 only)',
    0x1B6C24: 'unreachable: delay slot of the phase >= 2 exit',
    0x1B6C44: 'dead', 0x1B6E88: 'dead',
    0x1B7000: 'unreachable: 001B6FA0 phase >= 5 (it writes phases 0..4 only)',
    0x1B7004: 'unreachable: delay slot of the phase >= 5 exit',
    0x1B7094: 'dead', 0x1B7350: 'dead',
    0x1B78D0: 'unreachable: 001B7840 sub3 phase >= 2 (it writes phases 0 and 1 only)',
    0x1B78D4: 'unreachable: delay slot of that exit', 0x1B78D8: 'dead',
    0x1B79B4: 'unreachable: 001B7840 sub9 phase >= 2 (it writes phases 0 and 1 only)',
    0x1B79B8: 'unreachable: delay slot of that exit',
    0x1B8238: 'dead', 0x1B8244: 'dead', 0x1B8250: 'dead', 0x1B825C: 'dead', 0x1B8268: 'dead',
}


def handler_case(case):
    COVERAGE.clear()
    result = CONTEXT['asr'].run_scenario(case)
    return result[0], result[1], sorted(COVERAGE)


def part1():
    import test_area_script_reference as asr
    asr.BUILD = OUT / 'area_script'
    # A larger generated-record arena (same base) so the extra scripts fit.
    asr.SYNTHETIC = (asr.SYNTHETIC[0], asr.SYNTHETIC[0] + 0x4000)
    ram_bytes = (asr.REF / 'playable_ee.bin').read_bytes()
    asr.CONTEXT.update(elf=CONTEXT['elf'], ram=ram_bytes, spad=bytes(0x4000), lib=asr.build(),
                       synthetic=asr.synthetic_arena()[1].ljust(asr.SYNTHETIC[1] - asr.SYNTHETIC[0],
                                                                b'\0'))
    CONTEXT['asr'] = asr
    load = asr.RamOracle.load

    def traced(self, address, size=4):
        if size == 4:
            for entry, length in HANDLERS.items():
                if entry <= address < entry + length:
                    COVERAGE.add(address)
        return load(self, address, size)
    asr.RamOracle.load = traced
    cases = [c for c in asr.scenarios() if c[0] in HANDLER_SCENARIOS]
    assert len(cases) == len(HANDLER_SCENARIOS), [c[0] for c in cases]
    CONTEXT['asr_ram'] = ram_bytes
    extra, arena = extra_scripts(asr)
    asr.CONTEXT['synthetic'] = arena.ljust(asr.SYNTHETIC[1] - asr.SYNTHETIC[0], b'\0')
    on_call = asr.Env.on_call

    def env_on_call(self, name, args, mem):
        if name == 'w_001CA700' and 'ca700' in self.s:
            return self.s['ca700']
        return on_call(self, name, args, mem)
    asr.Env.on_call = env_on_call
    cases += [(label, entry, asr.SYNTHETIC_OWNER, asr.SYNTHETIC, scenario)
              for label, entry, scenario in extra]
    total = len(cases)
    if not FULL:
        cases = [c for c in cases if c[0] in QUICK_COVER]
        assert len(cases) == len(QUICK_COVER), sorted(set(QUICK_COVER) - {c[0] for c in cases})
    results = parallel_map(handler_case, cases)
    seen = set()
    ticks = 0
    for label, count, cov in results:
        seen.update(cov); ticks += count
    report, left = [], []
    for entry, length in sorted(HANDLERS.items()):
        ran = sum(1 for a in seen if entry <= a < entry + length)
        assert entry in seen, ('handler never executed', hex(entry))
        report.append(f'{entry:06X} {ran}/{length // 4}')
        left += [a for a in range(entry, entry + length, 4) if a not in seen]
    undocumented = [hex(a) for a in left if a not in UNEXECUTED]
    assert not undocumented, ('handler instructions no case executes', undocumented)
    print('part 1: em_area_script lockstep passes on', len(cases), 'of', total, 'scenarios,', ticks,
          'ticks; original handler instructions executed:', ', '.join(report))
    return dict(scenarios=len(cases), ticks=ticks, handlers=report)


# ======================================================================
# Part 2: em_script_door_fan.c
# ======================================================================

class Env:
    """One scripted answer per call, shared by both sides; each side logs its
    own calls and the logs are compared."""
    def __init__(self, **script):
        self.s = script
        self.count = {}

    def next(self, name, default=0):
        n = self.count.get(name, 0)
        self.count[name] = n + 1
        values = self.s.get(name, default)
        if values == 'tracks': return TRACKS + 0x40 * (n % 8)
        if values == 'handles': return HANDLES + 0x10 * n
        if callable(values): return values(n)
        if isinstance(values, (list, tuple)): return values[n] if n < len(values) else values[-1]
        return values


def sdf_workers(env, calls, mem, views, keep):
    """Native workers answering from `env` and the native memory mirror `mem`
    (a dict address -> byte over the captured RAM)."""
    def rd(address, size):
        return sum(mem.get(address + i, CONTEXT['base'][address + i]) << (8 * i)
                   for i in range(size))

    def w(name, fn):
        t = dict(SDF_WORKERS)[name]
        cb = t(fn)
        keep.append(cb)
        return cb

    def r_28a490(_, index, out):
        out[0] = rd(0x28A490 + 4 * index, 4); return 0

    def r_track(_, address, out):
        out[0] = fnum(rd(address, 4)); return 0

    def r_24db80(_, address, out):
        out[0] = rd(address, 2); return 0

    def alloc(name):
        def fn(_, *args):
            node_out, view_out = args[-2], args[-1]
            calls.append((name,) + tuple(args[:-2]))
            node = env.next(name)
            node_out[0] = node
            if node:
                view_out[0] = C.pointer(views[node])
            return 0
        return fn

    def rec(name, result=None, out=None, cast=None):
        def fn(_, *args):
            values = []
            for a in args[:len(args) - (1 if out else 0)]:
                if isinstance(a, float): values.append(fb(a))
                elif isinstance(a, int): values.append(a)
                else: values.append('obj')
            calls.append((name,) + tuple(values))
            value = env.next(name) if out else None
            if out: args[-1][0] = cast(value) if cast else value
            if name == 'w_001CA6E0': args[0][0].w44 = MODEL(args[1])
            if name == 'w_001C5C90': args[0][0].lifecycle = env.next('lifecycle_after_5C90')
            return 0
        return fn
    table = {
        'r_0028A490': r_28a490, 'r_track_head': r_track, 'r_0024DB80': r_24db80,
        'w_001AFA90': alloc('w_001AFA90'), 'w_001C8140': alloc('w_001C8140'),
        'w_001C6120': rec('w_001C6120', out=True),
        'w_001C6150': rec('w_001C6150', out=True), 'w_001AF780': rec('w_001AF780', out=True),
        'w_001C61D0': rec('w_001C61D0', out=True), 'w_001B1630': rec('w_001B1630', out=True),
        'w_001C64F0': rec('w_001C64F0', out=True, cast=s16),
    }
    for name, _ in SDF_WORKERS:
        if name not in table: table[name] = rec(name)
    return SdfWorkers(None, *[w(n, table[n]) for n, _ in SDF_WORKERS])


def MODEL(bank): return (0x00C00000 + (bank & 0xFFF0)) & MASK


def sdf_hooks(o, env, calls):
    def rec(name, kinds, result=False, cast=None):
        def fn(e):
            values = []
            ireg, freg = 4, 12
            for k in kinds:
                if k == 'f': values.append(e.f[freg] & MASK); freg += 1
                elif k == 'o': ireg += 1; values.append('obj')
                elif k == 'x':
                    assert e.r[ireg] & MASK in (ACT, OWNER), (name, 'argument is not the actor')
                    ireg += 1
                elif k == 'h': values.append(s16(e.r[ireg])); ireg += 1
                elif k == 'b': values.append(e.r[ireg] & 0xFF); ireg += 1
                elif k == 'i': values.append(s32(e.r[ireg])); ireg += 1
                else: values.append(e.r[ireg] & MASK); ireg += 1
            calls.append((name,) + tuple(values))
            if name == 'w_001CA6E0': e.save(e.r[4] + 0x44, MODEL(e.r[5]))
            if name == 'w_001C5C90': e.save(e.r[4] + 4, env.next('lifecycle_after_5C90'), 1)
            if result:
                value = env.next(name)
                e.r[2] = sx32(value)
        return fn
    table = {
        0x1AFA90: ('w_001AFA90', 'b', True), 0x1C8140: ('w_001C8140', 'uhu', True),
        0x1CA6E0: ('w_001CA6E0', 'ou', False), 0x1C6120: ('w_001C6120', 'ui', True),
        0x22EC30: ('w_0022EC30', 'u', False), 0x1C5C90: ('w_001C5C90', 'o', False),
        0x1C6150: ('w_001C6150', 'u', True), 0x1AF780: ('w_001AF780', '', True),
        0x1BA8E0: ('w_001BA8E0', 'oh', False), 0x1D8BF0: ('w_001D8BF0', 'oi', False),
        0x1CA6F0: ('w_001CA6F0', 'ob', False), 0x1CB5B0: ('w_001CB5B0', 'b', False),
        0x1C63E0: ('w_001C63E0', 'oh', False), 0x1C61D0: ('w_001C61D0', 'uh', True),
        0x1C67E0: ('w_001C67E0', 'xhff', False), 0x1B1630: ('w_001B1630', 'fff', True),
        0x1B1B70: ('w_001B1B70', 'x', False), 0x1C64F0: ('w_001C64F0', 'xf', True),
        0x1BC150: ('w_001BC150', 'x', False),
    }
    for address, (name, kinds, result) in table.items():
        hook(o, address, rec(name, kinds, result))


def new_oracle():
    o = Oracle(CONTEXT['base'])
    return o


def assert_written(o, allowed, label):
    stray = sorted(a for a in o.written if a not in allowed)
    assert not stray, (label, 'original writes outside the modelled bytes',
                       [hex(a) for a in stray[:12]])


def span(address, size): return set(range(address, address + size))


def check_fault(fault, label):
    assert fault.code == 0, (label, 'native fault', hex(fault.address), fault.code)


# ---- 001BA510 ------------------------------------------------------------

def case_ba510(seed):
    rng = random.Random(seed)
    o = new_oracle()
    before = [rng.randrange(1, 256) for _ in range(0x14)]
    o.seed_bytes(0x8106D0, bytes(before))
    run_original(o, 0x1BA510)
    activity = (C.c_uint8 * 12)(*before[4:16])
    fault = Fault()
    assert CONTEXT['lib'].em_sdf_001BA510(activity, C.byref(fault)) == 0
    check_fault(fault, 'BA510')
    after = [o.load(0x8106D0 + i, 1) for i in range(0x14)]
    assert after[:4] == before[:4] and after[16:] == before[16:], 'BA510 touched a neighbour'
    assert after[4:16] == list(activity), ('BA510', after, list(activity))
    assert_written(o, span(0x8106D4, 12), 'BA510')
    return o.outcomes


# ---- 001BAC00 ------------------------------------------------------------

def entry_bytes(b0, b2, tag, s6, s8, cmd, pos, rot, handler):
    return struct.pack('<BBBBhhhh4x6fI', b0, 0, b2, 0x5A, tag, s6, s8, cmd, *pos, *rot, handler)


def synthetic_list(entries):
    data = b''.join(entry_bytes(*e) for e in entries)
    return data + struct.pack('<h', -1) + b'\0' * 0x2A


SPAWN_FIELDS = [(0x03, 1, 'b03'), (0x0D, 1, 'b0D'), (0x10, 4, 'handler_10'), (0x20, 4, 'entry_20'),
                (0x24, 4, 'owner_24'), (0x2E, 2, 's2E')]


def case_bac00(case):
    label, list_address, data, nodes, alt_nodes = case
    env = Env(w_001AFA90=nodes, w_001C8140=alt_nodes)
    o = new_oracle()
    ocalls = []
    sdf_hooks(o, env, ocalls)
    o.seed(OWNER + 0x14, OWNER)
    o.seed(OWNER + 0x2E, 0x1234, 2)
    o.seed(REC + 0x14, list_address)
    if data is not None:
        o.seed_bytes(list_address, data)
    all_nodes = [n for n in list(nodes) + list(alt_nodes) if n]
    pattern = bytes((0xA5 + i) & 255 for i in range(0x100))
    for n in all_nodes: o.seed_bytes(n, pattern)
    result = run_original(o, 0x1BAC00, (OWNER, OWNER + 0x1F0, REC))
    # Native side.
    env_n = Env(w_001AFA90=nodes, w_001C8140=alt_nodes)
    ncalls, keep = [], []
    views = {}
    for n in all_nodes:
        v = Spawned()
        v.b03, v.b0D = pattern[3], pattern[0xD]
        v.handler_10, v.entry_20, v.owner_24 = (struct.unpack_from('<I', pattern, 0x10)[0],
                                                struct.unpack_from('<I', pattern, 0x20)[0],
                                                struct.unpack_from('<I', pattern, 0x24)[0])
        v.s2E = struct.unpack_from('<h', pattern, 0x2E)[0]
        for i in range(3):
            v.pos_B0[i] = fnum(struct.unpack_from('<I', pattern, 0xB0 + 4 * i)[0])
            v.rot_C0[i] = fnum(struct.unpack_from('<I', pattern, 0xC0 + 4 * i)[0])
        views[n] = v
    mem = {}
    workers = sdf_workers(env_n, ncalls, mem, views, keep)
    if data is not None:
        image_bytes, base = data, list_address
    else:
        image_bytes, base = bytes(CONTEXT['base'][ARENA:ARENA + 0x7800]), ARENA
    buf = (C.c_ubyte * len(image_bytes)).from_buffer_copy(image_bytes)
    image = Image(buf, base, len(image_bytes))
    record = (C.c_ubyte * 0x40)(*[0] * 0x40)
    struct.pack_into('<I', record, 0x14, list_address)
    owner = SpawnOwner(OWNER, 0x1234)
    fault = Fault()
    got = CONTEXT['lib'].em_sdf_001BAC00(C.byref(owner), record, C.byref(image),
                                         C.byref(workers), C.byref(fault))
    check_fault(fault, label)
    assert (got, ncalls) == (result, ocalls), (label, got, result, ncalls, ocalls)
    assert owner.s2E == s16(o.load(OWNER + 0x2E, 2)), label
    allowed = span(OWNER + 0x2E, 2)
    for n, v in views.items():
        for off, size, name in SPAWN_FIELDS:
            value = o.load(n + off, size)
            mine = getattr(v, name) & ((1 << 8 * size) - 1)
            assert value == mine, (label, hex(n), name, hex(value), hex(mine))
            allowed |= span(n + off, size)
        for i in range(3):
            assert o.load(n + 0xB0 + 4 * i) == fb(v.pos_B0[i]), (label, 'pos', i)
            assert o.load(n + 0xC0 + 4 * i) == fb(v.rot_C0[i]), (label, 'rot', i)
        allowed |= span(n + 0xB0, 12) | span(n + 0xC0, 12)
    assert_written(o, allowed, label)
    return o.outcomes


def bac00_cases():
    rng = random.Random(0x1BAC00)
    N = [NODES + 0x300 * k for k in range(8)]
    out = []
    opening_list = 0x828F30
    out.append(('opening list, both spawn', opening_list, None, [N[0], N[1]], []))
    out.append(('opening list, first alloc fails', opening_list, None, [0, N[1]], []))
    out.append(('opening list, both fail', opening_list, None, [0, 0], []))
    pos = lambda: tuple(rng.uniform(-500, 500) for _ in range(3))
    for cmd, handler in itertools.product((0, 3, 5, -1), (0, 0x823F00)):
        out.append((f'one entry cmd {cmd} handler {handler:x}', LIST,
                    synthetic_list([(9, 1, 0x47, 5, 2, cmd, pos(), pos(), handler)]), [N[2]], []))
    out.append(('0x270E then plain then 0x270E', LIST, synthetic_list([
        (3, 0, 0x270E, 7, 4, 0, pos(), pos(), 0x823F10),
        (8, 2, 0x6B, 0, 0, 5, pos(), pos(), 0),
        (4, 0, 0x270E, -2, -1, 3, pos(), pos(), 0)]), [N[3]], [N[4], 0]))
    out.append(('0x270E returns nothing', LIST, synthetic_list([
        (3, 0, 0x270E, 1, 1, 0, pos(), pos(), 0)]), [], [0]))
    first = bytearray(synthetic_list([(0xFF, 0xFF, 0x12, 1, 1, 0, pos(), pos(), 0)]))
    first[0:2] = b'\xff\xff'   # the do-while spawns the first entry before any sentinel test
    out.append(('first entry has the sentinel short', LIST, bytes(first), [N[5]], []))
    four = [(k + 1, k, 0x40 + k, k, k, k % 9, pos(), pos(), 0) for k in range(4)]
    out.append(('four entries, the third fails', LIST, synthetic_list(four),
                [N[6], N[7], 0, N[0]], []))
    return out


# ---- 001BAD40 ------------------------------------------------------------

ACTOR_FIELDS = [(0x04, 1, 'lifecycle'), (0x09, 1, 'b09'), (0x0C, 1, 'b0C'), (0x18, 4, 'w18'),
                (0x40, 4, 'bank_40'), (0x44, 4, 'w44')]
GLOBALS = [('d2821B0', 0x2821B0, 4), ('d2821B4', 0x2821B4, 4), ('d2821B8', 0x2821B8, 4),
           ('d8106C0', 0x8106C0, 4), ('d810250', 0x810250, 4), ('d810254', 0x810254, 4),
           ('d810258', 0x810258, 4), ('d8101E4', 0x8101E4, 1), ('d81024E', 0x81024E, 2),
           ('d275BCC', 0x275BCC, 2), ('d8106B8', 0x8106B8, 1)]
GLOBAL_CT = {'d2821B0': C.c_int32, 'd2821B4': C.c_int32, 'd2821B8': C.c_int32,
             'd8106C0': C.c_uint32, 'd810250': C.c_uint32, 'd810254': C.c_float,
             'd810258': C.c_float, 'd8101E4': C.c_uint8, 'd81024E': C.c_int16,
             'd275BCC': C.c_int16, 'd8106B8': C.c_uint8}


def world_from(o):
    cells = {}
    for name, address, size in GLOBALS:
        ct = GLOBAL_CT[name]
        raw = o.load(address, size)
        cell = ct(fnum(raw)) if ct is C.c_float else ct(raw if ct in (C.c_uint32, C.c_uint8)
                                                         else (s16(raw) if size == 2 else s32(raw)))
        cells[name] = cell
    world = World(*[C.pointer(cells[name]) for name, _, _ in GLOBALS])
    return world, cells


def compare_world(o, cells, label):
    for name, address, size in GLOBALS:
        cell = cells[name]
        mine = fb(cell.value) if isinstance(cell, C.c_float) else cell.value & ((1 << 8 * size) - 1)
        assert o.load(address, size) == mine, (label, name, hex(o.load(address, size)), hex(mine))


def event_entry(msg, s6, s8, cmd, w28):
    return struct.pack('<4xhhhh', msg, s6, s8, cmd) + b'\0' * 0x1C + struct.pack('<I', w28)


def actor_view(o, address):
    v = EventActor()
    for off, size, name in ACTOR_FIELDS:
        setattr(v, name, o.load(address + off, size))
    for i in range(0x78): v.bones_110[i] = o.load(address + 0x110 + 4 * i)
    return v


def compare_actor(o, address, v, label):
    for off, size, name in ACTOR_FIELDS:
        assert o.load(address + off, size) == getattr(v, name), (label, name,
                                                                 hex(o.load(address + off, size)),
                                                                 hex(getattr(v, name)))
    for i in range(0x78):
        assert o.load(address + 0x110 + 4 * i) == v.bones_110[i], (label, 'bone', i)


def case_bad40(case):
    label, entry_address, entry, script, cap, lifecycle = case
    env = Env(**script)
    o = new_oracle()
    ocalls = []
    sdf_hooks(o, env, ocalls)
    rng = random.Random(label)
    o.seed_bytes(ACT, bytes(rng.randrange(256) for _ in range(0x2F0)))
    o.seed(ACT + 4, lifecycle, 1)
    o.seed(0x275BCC, cap & 0xFFFF, 2)
    for k in range(8):   # a float at every track address 001C6120 can return
        o.seed(TRACKS + 0x40 * k, bits(1.5 + k))
    if entry is not None:
        o.seed_bytes(entry_address, entry)
    ev = bytes(o.load(entry_address + i, 1) for i in range(0x2C))
    view = actor_view(o, ACT)
    world, cells = world_from(o)
    snapshot = dict(o.over)
    result = run_original(o, 0x1BAD40, (ACT, entry_address))
    env_n = Env(**script)
    ncalls, keep = [], []
    workers = sdf_workers(env_n, ncalls, snapshot, {}, keep)
    fault = Fault()
    evbuf = (C.c_ubyte * 0x2C).from_buffer_copy(ev)
    got = CONTEXT['lib'].em_sdf_001BAD40(C.byref(view), evbuf, C.byref(world), C.byref(workers),
                                         C.byref(fault))
    check_fault(fault, label)
    assert (s32(result), ncalls) == (got, ocalls), (label, s32(result), got, ocalls, ncalls)
    compare_actor(o, ACT, view, label)
    compare_world(o, cells, label)
    allowed = (span(ACT + 4, 1) | span(ACT + 9, 1) | span(ACT + 0xC, 1) | span(ACT + 0x40, 8) |
               span(ACT + 0x110, 0x1E0))
    for _, address, size in GLOBALS: allowed |= span(address, size)
    assert_written(o, allowed, label)
    return o.outcomes


def bad40_cases():
    out = []
    base_script = dict(w_001C6120='tracks', w_001C6150=3, w_001AF780='handles',
                       w_001C61D0=57, lifecycle_after_5C90=1)
    out.append(('msg 0x270D', ENT, event_entry(0x270D, 3, -7, 0, 0), base_script, 0x40, 1))
    out.append(('msg 0x270C', ENT, event_entry(0x270C, 3, 7, 4, 0), base_script, 0x40, 1))
    sweep = []
    for cmd in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0x7FFF, -1):
        for count, cap in ((0, 0x40), (2, 0x40), (3, 0x40), (5, 0x40), (3, 2), (3, 3), (1, -1),
                           (0x78, 0x78)):
            for frames, after in ((57, 1), (0, 2), (-3, 0)):
                script = dict(base_script, w_001C6150=count, w_001C61D0=frames,
                              lifecycle_after_5C90=after)
                sweep.append((f'cmd {cmd} count {count} cap {cap} frames {frames} 5C90 {after}',
                              ENT, event_entry(0x47, 0x98, 2, cmd, 0x1234ABCD), script, cap, 1))
    out += select(sweep, 60, 0x1BAD40, axes=(lambda c: c[0].split()[1], lambda c: c[4],
                                             lambda c: c[3]['w_001C6150'],
                                             lambda c: c[3]['lifecycle_after_5C90']))
    for after in (0, 1, 2, 3):
        out.append((f'cmd 5 lifecycle after 001C5C90 {after}', ENT, event_entry(0x6B, 0, 0, 5, 0),
                    dict(base_script, lifecycle_after_5C90=after), 0x40, 0))
    for msg in (0x47, -5, 0x6B):
        out.append((f'msg {msg} cmd 0 bank index', ENT, event_entry(msg, 7, 1, 0, 0), base_script,
                    0x40, 1))
    # The opening's own entries (from the captured overlay).
    out.append(('opening entry 0x828F30', 0x828F30, None, dict(base_script, w_001C6150=21),
                0x40, 1))
    out.append(('opening entry 0x828F5C', 0x828F5C, None, base_script, 0x40, 1))
    return out


# ---- 001B1B30 ------------------------------------------------------------

def case_b1b30(case):
    label, visible, xyz, answer = case
    env = Env(w_001B1630=answer)
    o = new_oracle()
    ocalls = []
    sdf_hooks(o, env, ocalls)
    o.seed(ACT + 1, visible, 1)
    result = run_original(o, 0x1B1B30, (ACT,), xyz)
    vis = C.c_uint8(visible)
    ncalls, keep = [], []
    workers = sdf_workers(Env(w_001B1630=answer), ncalls, {}, {}, keep)
    fault = Fault()
    got = CONTEXT['lib'].em_sdf_001B1B30(C.byref(vis), *[fnum(v) for v in xyz], C.byref(workers),
                                         C.byref(fault))
    check_fault(fault, label)
    assert (result, ocalls, o.load(ACT + 1, 1)) == (got, ncalls, vis.value), (label, result, got,
                                                                              ocalls, ncalls)
    assert_written(o, span(ACT + 1, 1), label)
    return o.outcomes


def b1b30_cases():
    rng = random.Random(0x1B1B30)
    out = []
    for visible, answer in itertools.product((0, 1, 7), (0, 1)):
        xyz = tuple(bits(rng.uniform(-1000, 1000)) for _ in range(3))
        out.append((f'visible {visible} answer {answer}', visible, xyz, answer))
    out.append(('denormal and -0 arguments', 3, (0x00000001, 0x80000000, 0x7F7FFFFF), 1))
    return out


# ---- 001BC240 / 001BC290 ---------------------------------------------------

def case_door(case):
    label, entry, b0b, flags_in, answer, b8 = case
    env = Env(w_001C64F0=answer)
    o = new_oracle()
    ocalls = []
    sdf_hooks(o, env, ocalls)
    o.seed(ACT + 0xB, b0b, 1)
    o.seed(ACT + 0x1FE, flags_in, 2)
    o.seed(0x8106B8, b8, 1)
    result = run_original(o, entry, (ACT, ACT + 0x1F0))
    door = DoorStep(b0b, s16(flags_in))
    world, cells = world_from(new_oracle_with(0x8106B8, b8))
    ncalls, keep = [], []
    workers = sdf_workers(Env(w_001C64F0=answer), ncalls, {}, {}, keep)
    fault = Fault()
    if entry == 0x1BC240:
        got = CONTEXT['lib'].em_sdf_001BC240(C.byref(door), C.byref(workers), C.byref(fault))
    else:
        got = CONTEXT['lib'].em_sdf_001BC290(C.byref(door), C.byref(world), C.byref(workers),
                                             C.byref(fault))
    check_fault(fault, label)
    if entry == 0x1BC290:
        assert s32(result) == got, (label, result, got)
    assert ocalls == ncalls, (label, ocalls, ncalls)
    assert (o.load(ACT + 0xB, 1), s16(o.load(ACT + 0x1FE, 2))) == (door.b0B, door.anim_flags), label
    assert_written(o, span(ACT + 0xB, 1) | span(ACT + 0x1FE, 2), label)
    return o.outcomes


def new_oracle_with(address, value):
    o = new_oracle()
    o.seed(address, value, 1)
    return o


def door_cases():
    out = []
    for entry, b0b, answer, b8 in itertools.product((0x1BC240, 0x1BC290), (0, 4, 0xFF),
                                                    (0, 1, 0x7FFF, 0x8000, 0x1234), (0, 1, 2)):
        out.append((f'{entry:06X} b0B {b0b} flags {answer:#x} B8 {b8}', entry, b0b, 0x5555,
                    answer, b8))
    return select(out, 40, 0x1BC240, axes=(lambda c: c[1], lambda c: c[4], lambda c: c[5]))


# ---- 001BBD60 ------------------------------------------------------------

def case_bbd60(cases):
    outcomes = set()
    for label, link, side in cases:
        o = new_oracle()
        o.seed(ACT + 0x56, link & 0xFFFF, 2)
        o.seed(ACT + 0x2E, side, 2)
        o.seed(REC + 0x18, 0xDEADBEEF)
        run_original(o, 0x1BBD60, (ACT, REC))
        word = C.c_uint32(0xDEADBEEF)
        ncalls, keep = [], []
        workers = sdf_workers(Env(), ncalls, {}, {}, keep)
        fault = Fault()
        assert CONTEXT['lib'].em_sdf_001BBD60(s16(link), side, C.byref(word), C.byref(workers),
                                              C.byref(fault)) == 0
        check_fault(fault, label)
        assert o.load(REC + 0x18) == word.value, (label, hex(o.load(REC + 0x18)), hex(word.value))
        assert_written(o, span(REC + 0x18, 4), label)
        outcomes |= o.outcomes
    return outcomes


def bbd60_cases():
    out = []
    for row in range(256):
        for side in (0, 1):
            out.append((f'row {row} side {side}', (row << 8) | (row * 37 & 0xFF), side))
    out += [('negative link', 0x80FF, 1), ('side 2', 0x0100, 2), ('side 0xFFFF', 0x0200, 0xFFFF)]
    keep = lambda i, c: c[0] in ('negative link', 'side 2', 'side 0xFFFF')
    chosen = select(out, 48, 0x1BBD60, keep=keep)
    return [chosen[i::4] for i in range(4)]


def check_bbd60_table():
    """The halfwords the doors read come from ELF data: the captured RAM rows
    equal the user's ELF file (the binder reads the ELF)."""
    elf = CONTEXT['elf']
    for address in range(0x24DB80, 0x24DB80 + 0x40, 2):
        in_elf = struct.unpack_from('<H', elf, address - 0x100000 + 0x300)[0]
        assert CONTEXT['base'][address] | CONTEXT['base'][address + 1] << 8 == in_elf, hex(address)


# ---- 001B0080 ------------------------------------------------------------

LEAVES = {  # the VU0 / wrap leaves: (inputs, outputs) as (address arg, word count)
    0x1B1470: ('w_001B1470', (), ()), 0x1029C0: ('w_001029C0', (), ((0, 16),)),
    0x102C58: ('w_00102C58', ((1, 16), (2, 4)), ((0, 16),)),
    0x1026A0: ('w_001026A0', ((1, 16), (2, 4)), ((0, 4),)),
}


def leaf_hooks(o, log):
    """Run each leaf as original instructions and log its inputs/outputs."""
    for address, (name, ins, outs) in LEAVES.items():
        def fn(e, address=address, name=name, ins=ins, outs=outs):
            args = [e.r[4 + i] & MASK for i in range(3)]
            read = lambda spec: tuple(tuple(e.load(args[a] + 4 * k) for k in range(n))
                                      for a, n in spec)
            inputs = read(ins) + ((e.f[12] & MASK,) if name == 'w_001B1470' else ())
            hooked = e.hooks.pop(address)
            v0, f0 = nested_bits(e, address, args, (e.f[12],))
            e.hooks[address] = hooked
            outputs = read(outs) + ((f0 & MASK,) if name == 'w_001B1470' else ())
            log.append((name, inputs, outputs))
            e.r[2] = v0; e.f[0] = f0
        hook(o, address, fn)


def leaf_workers(log, keep):
    """Native leaf workers replaying the oracle's log (inputs asserted)."""
    queue = list(log)
    t = dict(SDF_WORKERS)

    def take(name, inputs):
        got = queue.pop(0)
        assert got[0] == name and got[1] == inputs, (name, got, inputs)
        return got[2]

    def wrap(_, angle, out):
        out[0] = fnum(take('w_001B1470', (fb(angle),))[0]); return 0

    def ident(_, m):
        for k, v in enumerate(take('w_001029C0', ())[0]): m[k] = v
        return 0

    def rot(_, dst, src, angles):
        out = take('w_00102C58', (tuple(src[k] for k in range(16)),
                                  tuple(fb(angles[k]) for k in range(4))))[0]
        for k, v in enumerate(out): dst[k] = v
        return 0

    def mul(_, out, m, v):
        res = take('w_001026A0', (tuple(m[k] for k in range(16)), tuple(v[k] for k in range(4))))[0]
        for k, value in enumerate(res): out[k] = fnum(value)
        return 0
    cbs = {'w_001B1470': t['w_001B1470'](wrap), 'w_001029C0': t['w_001029C0'](ident),
           'w_00102C58': t['w_00102C58'](rot), 'w_001026A0': t['w_001026A0'](mul)}
    keep.extend(cbs.values())
    return cbs, queue


def case_b0080(case):
    label, area, room, player, angles, distance, a1 = case
    o = new_oracle()
    cam = 0x8101E0
    o.seed(0x810700, area, 1); o.seed(0x810702, room, 1)
    for k in range(4): o.seed(0x810350 + 4 * k, player[k])
    for k in range(4): o.seed(0x70003B50 + 4 * k, angles[k])
    o.seed(cam + 0xC, distance)
    log = []
    leaf_hooks(o, log)
    words = lambda a, n: [o.load(a + 4 * k) for k in range(n)]
    seat = Seat(fnum(distance), (C.c_float * 4)(*map(fnum, words(cam + 0x10, 4))),
                (C.c_float * 4)(*map(fnum, words(cam + 0x20, 4))),
                (C.c_float * 4)(*map(fnum, words(cam + 0x30, 4))))
    cells = dict(a=C.c_uint8(area), r=C.c_uint8(room),
                 p=(C.c_float * 4)(*map(fnum, player)), g=(C.c_float * 4)(*map(fnum, angles)),
                 eye=(C.c_float * 4)(*map(fnum, words(0x8105D0, 4))),
                 tgt=(C.c_float * 4)(*map(fnum, words(0x8105E0, 4))),
                 m=(C.c_uint32 * 16)(*words(0x70003400, 16)), v=(C.c_uint32 * 4)(*words(0x70003600, 4)))
    run_original(o, 0x1B0080, (cam,), (a1,))
    keep = []
    workers = sdf_workers(Env(), [], {}, {}, keep)
    cbs, queue = leaf_workers(log, keep)
    for name, cb in cbs.items(): setattr(workers, name, cb)
    world = SeatWorld(C.pointer(cells['a']), C.pointer(cells['r']), cells['p'], cells['g'],
                      cells['eye'], cells['tgt'], cells['m'], cells['v'])
    fault = Fault()
    assert CONTEXT['lib'].em_sdf_001B0080(C.byref(seat), fnum(a1), C.byref(world),
                                          C.byref(workers), C.byref(fault)) == 0
    check_fault(fault, label)
    assert not queue, (label, 'native skipped leaf calls', queue)
    mine = ([fb(x) for x in seat.eye_10] + [fb(x) for x in seat.tgt_20] +
            [fb(x) for x in seat.rot_30])
    assert words(cam + 0x10, 12) == mine, (label, [hex(x) for x in words(cam + 0x10, 12)],
                                           [hex(x) for x in mine])
    assert words(0x8105D0, 4) == [fb(x) for x in cells['eye']], label
    assert words(0x8105E0, 4) == [fb(x) for x in cells['tgt']], label
    assert words(0x70003400, 16) == list(cells['m']), label
    assert words(0x70003600, 4) == list(cells['v']), label
    assert_written(o, span(cam + 0x10, 0x30) | span(0x8105D0, 0x20) | span(0x70003400, 0x40) |
                   span(0x70003600, 0x10), label)
    return o.outcomes


def b0080_cases():
    rng = random.Random(0x1B0080)
    out = []
    for area, room in ((1, 4), (1, 3), (0x0B, 4), (0x0B, 0), (0, 4)):
        for n in range(pick(8, 4)):
            player = tuple(bits(rng.uniform(-900, 900)) for _ in range(3)) + (0x3F800000,)
            angles = tuple(bits(rng.uniform(-9.0, 9.0)) for _ in range(3)) + (0,)
            distance = bits(rng.choice((0.0, 25.0, -7.5, rng.uniform(1, 80))))
            a1 = bits(rng.choice((2.0, 0.0, -3.5)))
            out.append((f'area {area:#x} room {room} #{n}', area, room, player, angles, distance, a1))
    return out


# ---- captured opening actors (001BAC00 / 001BAD40 fields) ----------------

def check_opening_capture():
    """opening_ee.bin holds the two actors 001BAC00 spawned from 0x828F30.
    The native 001BAC00 is run over that capture's own entries and nodes and
    every field it writes is compared with the capture, except the second
    actor's +0xB0..+0xC8, which 001C5C90 (its command-5 handler, run every
    tick through 001BB0E0) rewrites. The native 001BAD40 is run on the first
    actor's entry with 001CA6E0 answered by the captured +0x44 and 001C6150
    by that model's byte +8 (001C6150 is `return byte [8]`); +0x40, +0x09
    and +0x0C are compared with the capture."""
    m = CONTEXT['images']['opening']
    rd = lambda a, n=4: int.from_bytes(m[a:a + n], 'little')
    nodes, owner = (0x7A96E0, 0x7AE920), 0x7A8E10
    assert rd(owner + 0x10) == 0x823E80 and all(rd(n + 0x10) == 0x1BB0E0 for n in nodes)
    views = {n: Spawned() for n in nodes}
    env = Env(w_001AFA90=list(nodes))
    ncalls, keep = [], []
    CONTEXT['base_saved'] = CONTEXT['base']
    CONTEXT['base'] = m
    try:
        workers = sdf_workers(env, ncalls, {}, views, keep)
        image_bytes = bytes(m[ARENA:ARENA + 0x7800])
        buf = (C.c_ubyte * len(image_bytes)).from_buffer_copy(image_bytes)
        image = Image(buf, ARENA, len(image_bytes))
        record = (C.c_ubyte * 0x40)()
        struct.pack_into('<I', record, 0x14, 0x828F30)
        so = SpawnOwner(rd(owner + 0x14), 0x55)
        fault = Fault()
        assert CONTEXT['lib'].em_sdf_001BAC00(C.byref(so), record, C.byref(image),
                                              C.byref(workers), C.byref(fault)) == 1
        check_fault(fault, 'opening capture')
        assert so.s2E == s16(rd(owner + 0x2E, 2))
        compared = 0
        for n, v in views.items():
            for off, size, name in SPAWN_FIELDS:
                assert rd(n + off, size) == getattr(v, name) & ((1 << 8 * size) - 1), (hex(n), name)
                compared += 1
            if n == nodes[0]:
                for i in range(3):
                    assert rd(n + 0xB0 + 4 * i) == fb(v.pos_B0[i])
                    assert rd(n + 0xC0 + 4 * i) == fb(v.rot_C0[i])
                    compared += 2
        # 001BAD40 on the first actor.
        n = nodes[0]
        obj = EventActor()
        obj.lifecycle = 0
        model = rd(n + 0x44)
        env2 = Env(w_001C6150=m[model + 8], w_001AF780=lambda k: rd(n + 0x110 + 4 * k),
                   w_001C61D0=1, lifecycle_after_5C90=0)
        calls2 = []
        workers2 = sdf_workers(env2, calls2, {}, {}, keep)
        # 001CA6E0 in the capture bound this model: answer +0x44 with it.
        def ca6e0(_, obj_p, bank):
            calls2.append(('w_001CA6E0', bank)); obj_p[0].w44 = model; return 0
        cb = dict(SDF_WORKERS)['w_001CA6E0'](ca6e0); keep.append(cb)
        workers2.w_001CA6E0 = cb
        world, cells = world_from(new_oracle())
        cells['d275BCC'].value = 0x40
        ev = (C.c_ubyte * 0x2C).from_buffer_copy(bytes(m[0x828F30:0x828F30 + 0x2C]))
        fault = Fault()
        assert CONTEXT['lib'].em_sdf_001BAD40(C.byref(obj), ev, C.byref(world),
                                              C.byref(workers2), C.byref(fault)) == 0
        check_fault(fault, 'opening 001BAD40')
        assert (obj.bank_40, obj.b09, obj.b0C) == (rd(n + 0x40), m[n + 9], m[n + 0xC]), (
            hex(obj.bank_40), obj.b09, obj.b0C)
        compared += 3
    finally:
        CONTEXT['base'] = CONTEXT.pop('base_saved')
    return compared


# ======================================================================
# Part 3: the AREA11 overlay owners
# ======================================================================

CREATURE_FIELDS = [(0x00, 1, 'b00'), (0x04, 1, 'lifecycle'), (0x28, 2, 'timer_28'),
                   (0x11C, 4, 'bone3_11C'), (0x1F4, 4, 'f1F4'), (0x1F8, 4, 'f1F8'),
                   (0x1FC, 4, 'f1FC'), (0x200, 4, 'w200'), (0x204, 4, 'w204'), (0x208, 4, 'w208'),
                   (0x20C, 4, 'w20C'), (0x210, 4, 'f210'), (0x214, 4, 'f214'), (0x218, 4, 'f218'),
                   (0x21C, 4, 'w21C'), (0x220, 4, 'child_220'), (0x224, 4, 'w224')]
CHILD_FIELDS = [(0x03, 1, 'b03'), (0x0D, 1, 'b0D'), (0x0E, 2, 'h0E'), (0x10, 4, 'handler_10'),
                (0x2E, 2, 'h2E'), (0x54, 2, 'h54'), (0x56, 2, 'h56'), (0x9A, 1, 'b9A'),
                (0x11C, 4, 'bone3_11C')]
PARTNER_FIELDS = [(0x00, 1, 'b00'), (0x04, 1, 'lifecycle'), (0x28, 2, 'timer_28'),
                  (0x34, 2, 'h34'), (0x36, 2, 'hit_36'), (0x9A, 1, 'item_9A')]
MANAGER_FIELDS = [(0x00, 1, 'b00'), (0x04, 1, 'lifecycle'), (0x05, 1, 'phase'), (0x28, 2, 'h28'),
                  (0x2E, 2, 'h2E')]


def get_field(v, name, size):
    value = getattr(v, name)
    if isinstance(value, float): return fb(value)
    return value & ((1 << 8 * size) - 1)


def set_fields(v, o, address, fields):
    for off, size, name in fields:
        raw = o.load(address + off, size)
        cur = getattr(v, name)
        if isinstance(cur, float): setattr(v, name, fnum(raw))
        elif size == 2 and C.c_int16 in [t for n, t in type(v)._fields_ if n == name]:
            setattr(v, name, s16(raw))
        elif size == 4 and C.c_int32 in [t for n, t in type(v)._fields_ if n == name]:
            setattr(v, name, s32(raw))
        else: setattr(v, name, raw)


def compare_fields(v, o, address, fields, label):
    for off, size, name in fields:
        assert o.load(address + off, size) == get_field(v, name, size), (
            label, name, hex(o.load(address + off, size)), hex(get_field(v, name, size)))


class Husk:
    """One original owner node and its native twin, ticked in lockstep."""

    def __init__(self, kind, image, node, env, seed=None, words=None):
        self.kind, self.node, self.env = kind, node, env
        self.seeded = dict(words or {})
        self.o = Oracle(image)
        self.ocalls, self.ncalls, self.keep = [], [], []
        self.sines = []
        self.native_sines = 0
        self.untranslated = None
        if seed: seed(self.o)
        o = self.o
        self.draw = o.load(node + 0x4C)
        self.install()
        self.flags = (C.c_uint8 * 256)(*[o.load(0x810758 + i, 1) for i in range(256)])
        self.c6c8 = C.c_uint32(o.load(0x8106C8))
        self.c808 = C.c_uint8(o.load(0x810808, 1))
        self.c3a20 = C.c_float(fnum(o.load(0x70003A20)))
        self.world = HuskWorld(self.flags, C.pointer(self.c6c8), C.pointer(self.c808),
                               C.pointer(self.c3a20))
        self.child = None
        if kind == 'creature':
            self.v = Creature()
            set_fields(self.v, o, node, CREATURE_FIELDS)
            for i in range(4):
                self.v.pos_B0[i] = o.load(node + 0xB0 + 4 * i)
                self.v.rot_C0[i] = o.load(node + 0xC0 + 4 * i)
            self.child_address = self.v.child_220 or None
            if self.child_address: self.child = self.child_view(self.child_address)
        elif kind == 'partner':
            self.v = Partner()
            set_fields(self.v, o, node, PARTNER_FIELDS)
            self.linked_address = o.load(node + 0x18)
            self.linked = Linked(o.load(self.linked_address + 4, 1),
                                 s32(o.load(self.linked_address + 0x21C)))
        else:
            self.v = Manager()
            set_fields(self.v, o, node, MANAGER_FIELDS)
        self.bone_mem = {}
        self.fault = Fault()
        self.workers = self.native_workers()

    def child_view(self, address):
        c = Child()
        set_fields(c, self.o, address, CHILD_FIELDS)
        for i in range(4):
            c.fA0[i] = fnum(self.o.load(address + 0xA0 + 4 * i))
            c.pos_B0[i] = self.o.load(address + 0xB0 + 4 * i)
            c.rot_C0[i] = self.o.load(address + 0xC0 + 4 * i)
        return c

    # ---- oracle hooks
    def install(self):
        o, env, calls, node = self.o, self.env, self.ocalls, self.node

        def plain(name, kinds=''):
            def fn(e):
                values, ireg, freg = [], 4, 12
                for k in kinds:
                    if k == 'f': values.append(e.f[freg] & MASK); freg += 1
                    elif k == 'b': values.append(e.r[ireg] & 0xFF); ireg += 1
                    elif k == 'i': values.append(s32(e.r[ireg])); ireg += 1
                    elif k == 'a':
                        assert e.r[ireg] & MASK == node, (name, 'argument is not the actor')
                        ireg += 1
                    else: values.append(e.r[ireg] & MASK); ireg += 1
                calls.append((name,) + tuple(values))
                return values
            return fn

        def with_result(name, kinds=''):
            base = plain(name, kinds)
            def fn(e):
                base(e)
                e.r[2] = sx32(env.next(name))
            return fn

        def b0fd0(e):
            plain('w_001B0FD0', 'a')(e)
            value = env.next('w_001B0FD0')
            if value == 0: e.save(node + 4, (e.load(node + 4, 1) + 1) & 255, 1)
            elif value == 1: e.save(node + 4, 3, 1)
            e.r[2] = value

        def sine(e):
            calls.append(('w_0011E2A8', e.f[12] & MASK))
            hooked = e.hooks.pop(0x11E2A8)
            value = nested_bits(e, 0x11E2A8, (), (e.f[12],))[1] & MASK
            e.hooks[0x11E2A8] = hooked
            self.sines.append(value)
            e.f[0] = value

        def alloc(e):
            plain('w_001AFA90', 'b')(e)
            value = env.next('w_001AFA90')
            e.r[2] = value

        def ba1a0(e):
            assert e.r[4] & MASK == node + 0x1F0
            calls.append(('w_001BA1A0', e.r[5] & MASK))

        def ba1f0(e):
            plain('w_001BA1F0', 'a')(e)
            e.r[2] = sx32(env.next('w_001BA1F0'))

        def marker(lifecycle):
            def fn(e):
                raise Untranslated(lifecycle)
            return fn
        table = {
            0x1C6380: plain('w_001C6380', 'a'), 0x1B17A0: plain('w_001B17A0', 'a'),
            0x1AFC10: plain('w_001AFC10', 'a'), 0x1B0FD0: b0fd0,
            0x122BB8: with_result('w_00122BB8'), 0x11E2A8: sine,
            0x1A2370: plain('w_001A2370', 'au'), 0x1AFA90: alloc,
            0x102958: plain('w_00102958', 'uu'), 0x1B11E0: with_result('w_001B11E0', 'b'),
            0x1B1190: plain('w_001B1190', 'b'), 0x1EFE00: with_result('w_001EFE00', 'ua'),
            0x1FBD50: plain('w_001FBD50', 'aiif'), 0x1BA1A0: ba1a0, 0x1BA1F0: ba1f0,
            0x1D2830: plain('w_001D2830', 'ii'), 0x1C1DC0: plain('w_001C1DC0'),
            0x1FABB0: plain('w_001FABB0'), 0x1AEE10: plain('w_001AEE10', 'ii'),
            0x1C4760: plain('w_001C4760', 'ii'), 0x1FAE70: plain('w_001FAE70', 'i'),
        }
        for address, fn in table.items(): hook(o, address, fn)
        draw = o.load(node + 0x4C)
        hook(o, draw, plain('w_draw_4C', 'a'))
        if self.kind == 'creature':
            for lifecycle, entry in UNTRANSLATED.items(): hook(o, entry, marker(lifecycle))

    # ---- native workers
    def native_workers(self):
        env, calls = self.env, self.ncalls
        table = {}

        def mk(name, fn):
            cb = dict(HUSK_WORKERS)[name](fn)
            self.keep.append(cb)
            return cb

        def plain(name, result=None):
            def fn(_, *args):
                values = []
                n = len(args) - (1 if result else 0)
                for a in args[:n]:
                    values.append(fb(a) if isinstance(a, float) else a)
                calls.append((name,) + tuple(values))
                if result == 'env': args[-1][0] = env.next(name)
                return 0
            return fn
        for name, _ in HUSK_WORKERS:
            table[name] = plain(name)
        for name in ('w_001B0FD0', 'w_00122BB8', 'w_001B11E0', 'w_001EFE00', 'w_001BA1F0'):
            table[name] = plain(name, 'env')

        def sine(_, x, out):
            calls.append(('w_0011E2A8', fb(x)))
            out[0] = fnum(self.sines[self.native_sines])
            self.native_sines += 1
            return 0

        def r_slot(_, index, out):
            out[0] = self.mem_word(self.mem_word(0x275B40) + 4 * index); return 0

        def s_bone(_, bone, offset, value):
            self.bone_mem[bone + offset] = fb(value); return 0

        def alloc(_, cls, node_out, view_out):
            calls.append(('w_001AFA90', cls))
            node = env.next('w_001AFA90')
            node_out[0] = node
            if node:
                self.child_address = node
                self.child = self.child_view(node)
                view_out[0] = C.pointer(self.child)
            return 0

        def r_child(_, address, view_out):
            assert address == self.child_address, (hex(address), self.child_address)
            view_out[0] = C.pointer(self.child); return 0

        def r_link(_, out):
            out[0] = C.pointer(self.linked); return 0

        def a2370(_, matrix):
            calls.append(('w_001A2370', matrix)); return 0
        table.update(w_0011E2A8=sine, r_00275B40=r_slot, s_bone_f32=s_bone, w_001AFA90=alloc,
                     r_child_220=r_child, r_link_18=r_link, w_001A2370=a2370)
        return HuskWorkers(None, *[mk(n, table[n]) for n, _ in HUSK_WORKERS])

    def mem_word(self, address):
        return self.seeded.get(address, int.from_bytes(bytes(self.o.base[address:address + 4]),
                                                        'little'))

    # ---- one tick
    def tick(self, label):
        o = self.o
        del self.ocalls[:]; del self.ncalls[:]
        o.written.clear()
        entry = {'creature': 0x825940, 'partner': 0x827490, 'manager': 0x823CE0}[self.kind]
        reached = None
        try:
            run_original(o, entry, (self.node,))
        except Untranslated as u:
            reached = u.lifecycle
        lib = CONTEXT['lib']
        if self.kind == 'creature':
            got = lib.em_husk_creature_tick(C.byref(self.v), C.byref(self.world),
                                            C.byref(self.workers), C.byref(self.fault))
        elif self.kind == 'partner':
            got = lib.em_husk_partner_tick(C.byref(self.v), C.byref(self.workers),
                                           C.byref(self.fault))
        else:
            got = lib.em_husk_manager_tick(C.byref(self.v), C.byref(self.world),
                                           C.byref(self.workers), C.byref(self.fault))
        if reached is not None:
            assert got == -1 and self.fault.code == 3 and \
                self.fault.address == UNTRANSLATED[reached], (label, got, self.fault.code,
                                                               hex(self.fault.address))
            assert self.ocalls == self.ncalls, (label, self.ocalls, self.ncalls)
            return 'untranslated'
        assert self.fault.code == 0, (label, 'native fault', hex(self.fault.address),
                                      self.fault.code)
        assert self.ocalls == self.ncalls, (label, self.ocalls, self.ncalls)
        freed = ('w_001AFC10',) in self.ocalls
        assert got == (0 if freed else 1), (label, got)
        allowed = set()
        fields = {'creature': CREATURE_FIELDS, 'partner': PARTNER_FIELDS,
                  'manager': MANAGER_FIELDS}[self.kind]
        if not freed:
            compare_fields(self.v, o, self.node, fields, label)
        for off, size, _ in fields: allowed |= span(self.node + off, size)
        if self.kind == 'creature':
            if self.child is not None:
                c, a = self.child, self.child_address
                compare_fields(c, o, a, CHILD_FIELDS, label)
                for i in range(4):
                    assert o.load(a + 0xA0 + 4 * i) == fb(c.fA0[i]), (label, 'child A0', i)
                    assert o.load(a + 0xB0 + 4 * i) == c.pos_B0[i], (label, 'child B0', i)
                    assert o.load(a + 0xC0 + 4 * i) == c.rot_C0[i], (label, 'child C0', i)
                for off, size, _ in CHILD_FIELDS: allowed |= span(a + off, size)
                allowed |= span(a + 0xA0, 0x30)
            for address, value in self.bone_mem.items():
                assert o.load(address) == value, (label, 'bone', hex(address))
                allowed |= span(address, 4)
            assert o.load(0x70003A20) == fb(self.c3a20.value), label
            allowed |= span(0x70003A20, 4)
        if self.kind == 'partner':
            la = self.linked_address
            assert (o.load(la + 4, 1), s32(o.load(la + 0x21C))) == (self.linked.lifecycle,
                                                                   self.linked.w21C), label
            allowed |= span(la + 4, 1) | span(la + 0x21C, 4)
        if self.kind == 'manager':
            assert (o.load(0x8106C8), o.load(0x810808, 1)) == (self.c6c8.value, self.c808.value), \
                label
            allowed |= span(0x8106C8, 4) | span(0x810808, 1)
        assert_written(o, allowed, label)
        return 'freed' if freed else 'ok'

    def set_flag(self, value):
        self.o.seed(0x810788, value, 1)
        self.flags[0x30] = value


class Untranslated(Exception):
    def __init__(self, lifecycle):
        super().__init__(lifecycle)
        self.lifecycle = lifecycle


def husk_capture_case(case):
    """Tick each owner once from a captured AREA11 RAM image."""
    name, kind, node, entry = case
    image = CONTEXT['images'][name]
    assert int.from_bytes(image[node + 0x10:node + 0x14], 'little') == entry, (name, kind)
    state = (image[node + 4], image[node + 5], image[0x810788])
    h = Husk(kind, image, node, Env())
    outcome = h.tick(f'{name} {kind}')
    return name, kind, state, outcome, h.o.outcomes, h.o.fetched


HUSK_NODES = (('creature', 0x7A6AD0, 0x825940), ('partner', 0x7A6DC0, 0x827490),
              ('manager', 0x7A9100, 0x823CE0))


def seeded_husk(kind, fields, env, flag=0, extra=()):
    """A synthetic node at a scratch address over the playable image."""
    node = {'creature': 0x01D50000, 'partner': 0x01D51000, 'manager': 0x01D52000}[kind]

    def seed(o):
        for off, size, value in fields: o.seed(node + off, value, size)
        o.seed(node + 0x4C, DRAW)
        o.seed(0x810788, flag, 1)
        for address, value, size in extra: o.seed(address, value, size)
    words = {address: value for address, value, size in extra if size == 4}
    h = Husk(kind, CONTEXT['base'], node, env, seed, words)
    return h


def husk_unit_case(case):
    label, kind, fields, script, flag, extra, ticks, changes = case
    h = seeded_husk(kind, fields, Env(**script), flag, extra)
    outcomes = []
    for t in range(ticks):
        if t in changes:
            for what, value in changes[t]:
                if what == 'flag': h.set_flag(value)
                elif what == 'hit':
                    h.o.seed(h.node + 0x36, value, 2); h.v.hit_36 = value
        result = h.tick(f'{label} t{t}')
        outcomes.append(result)
        if result in ('freed', 'untranslated'): break
    return label, outcomes, h.o.outcomes, h.o.fetched


BONE_EXTRA = ((0x275B40, SLOTS, 4), (SLOTS + 0xC, BONE, 4), (BONE + 0x78, 0x12345678, 4))


def husk_unit_cases():
    out = []
    creature_base = [(0x04, 1, 0), (0x00, 1, 0), (0xB0, 4, bits(310.0)), (0xB4, 4, bits(290.5)),
                     (0xB8, 4, bits(-12.25)), (0xBC, 4, bits(1.0)), (0xC0, 4, bits(0.5)),
                     (0xC4, 4, bits(-1.25)), (0xC8, 4, bits(2.0)), (0xCC, 4, 0x3F800000),
                     (0x11C, 4, SELF_BONE)]
    for flag, fd0, rand, child in itertools.product((0, 1, 0xFF), (0, 1),
                                                    (0, 0x7FFFFFFF, 0x12345678), (CHILD, 0)):
        out.append((f'creature setup flag {flag} fd0 {fd0} rand {rand:#x} child {child:#x}',
                    'creature', creature_base,
                    dict(w_001B0FD0=fd0, w_00122BB8=rand, w_001AFA90=child), flag, BONE_EXTRA,
                    1, {}))
    for lifecycle in (3, 5, 0x63, 0x65, 0xFF, 1, 4):
        out.append((f'creature lifecycle {lifecycle:#x}', 'creature',
                    creature_base[1:] + [(0x04, 1, lifecycle)], {}, 0, BONE_EXTRA, 1, {}))
    for flag in (0, 1, 0xFF):
        out.append((f'creature dormant flag {flag}', 'creature',
                    creature_base[1:] + [(0x04, 1, 0x64)], {}, flag, BONE_EXTRA, 1, {}))
    # Lifecycle 2 (both arms): angles below, above and at -pi/2; countdowns.
    for angle, step, w21c, w224, flag in itertools.product(
            (0.0, -1.0, -1.5707963, -1.6, -3.0, 0.3), (0.0625, 0.5), (0, 1, 5, -2),
            (0, 1), (0, 0xFF)):
        fields = creature_base[1:] + [(0x04, 1, 2), (0x1FC, 4, bits(angle)),
                                      (0x1F8, 4, bits(step)), (0x21C, 4, w21c & MASK),
                                      (0x224, 4, w224), (0x220, 4, CHILD)]
        out.append((f'creature swing a {angle} s {step} n {w21c} w224 {w224} flag {flag}',
                    'creature', fields, {}, flag,
                    BONE_EXTRA + ((CHILD + 0x11C, CHILD_BONE, 4), (CHILD + 0x10, 0x1C5680, 4)),
                    1, {}))
    exact = creature_base[1:] + [(0x04, 1, 2), (0x1FC, 4, 0xBFC90FDB), (0x1F8, 4, bits(0.25)),
                                 (0x21C, 4, 3), (0x224, 4, 1), (0x220, 4, CHILD)]
    out.append(('creature swing exactly -pi/2', 'creature', exact, {}, 0xFF,
                BONE_EXTRA + ((CHILD + 0x11C, CHILD_BONE, 4),), 1, {}))
    link_extra = ((LINKED + 4, 0x64, 1), (LINKED + 0x21C, 0, 4))
    for lifecycle, hit, fd0, taken, fx, timer in itertools.product(
            (0, 1, 2, 3, 7), (0, 1), (0, 1), (0, 1), (0, 1), (0, 8, 9, 10, 11, -3)):
        if lifecycle != 2 and timer != 0: continue
        if lifecycle != 0 and (fd0 or taken): continue
        if lifecycle != 1 and (hit or fx): continue
        fields = [(0x04, 1, lifecycle), (0x00, 1, 0), (0x28, 2, timer & 0xFFFF), (0x34, 2, 0),
                  (0x36, 2, hit), (0x9A, 1, 0x1D), (0x18, 4, LINKED)]
        out.append((f'partner lc {lifecycle} hit {hit} fd0 {fd0} taken {taken} fx {fx} t {timer}',
                    'partner', fields, dict(w_001B0FD0=fd0, w_001B11E0=taken, w_001EFE00=fx),
                    0, link_extra, 1, {}))
    for lifecycle, phase, flag, poll in itertools.product((0, 1, 2, 3, 4, 0xFF), (0, 1, 2),
                                                          (0, 1, 0xFF), (0, 1, 3)):
        if lifecycle != 1 and (phase or poll): continue
        if lifecycle == 1 and phase != 1 and poll: continue
        fields = [(0x04, 1, lifecycle), (0x05, 1, phase), (0x00, 1, 0), (0x28, 2, 0x77),
                  (0x2E, 2, 0x1234)]
        out.append((f'manager lc {lifecycle} phase {phase} flag {flag} poll {poll}', 'manager',
                    fields, dict(w_001BA1F0=poll), flag,
                    ((0x8106C8, 0x5A5AFFFF, 4), (0x810808, 0x11, 1)), 1, {}))
    # Multi-tick lockstep runs.
    out.append(('creature setup then dormant, flag rises', 'creature', creature_base,
                dict(w_001B0FD0=0, w_00122BB8=0x2468ACE1, w_001AFA90=CHILD), 0, BONE_EXTRA, 8,
                {5: [('flag', 1)], 6: [('flag', 0xFF)]}))
    out.append(('creature swing to -pi/2 and count down', 'creature',
                creature_base[1:] + [(0x04, 1, 2), (0x1FC, 4, bits(-1.2)), (0x1F8, 4, bits(0.05)),
                                     (0x21C, 4, 4), (0x224, 4, 0), (0x220, 4, CHILD)],
                {}, 0xFF, BONE_EXTRA + ((CHILD + 0x11C, CHILD_BONE, 4),), 20, {}))
    out.append(('partner setup, shot, falls', 'partner',
                [(0x04, 1, 0), (0x00, 1, 0), (0x28, 2, 0x55), (0x34, 2, 0), (0x36, 2, 0),
                 (0x9A, 1, 0x21), (0x18, 4, LINKED)],
                dict(w_001B0FD0=0, w_001B11E0=0, w_001EFE00=1), 0, link_extra, 16,
                {3: [('hit', 2)]}))
    out.append(('manager wait, start, poll, end', 'manager',
                [(0x04, 1, 0), (0x05, 1, 0), (0x00, 1, 0), (0x28, 2, 5), (0x2E, 2, 0)],
                dict(w_001BA1F0=[0, 0, 1]), 0, ((0x8106C8, 0xFFFFFFFF, 4), (0x810808, 0, 1)), 10,
                {3: [('flag', 1)]}))
    keep = lambda i, c: (c[6] > 1 or c[1] != 'creature' or 'setup' in c[0] or 'lifecycle' in c[0]
                         or 'exactly' in c[0])
    lifecycle = lambda c: (c[1], [v for off, _, v in c[2] if off == 4][-1])
    return select(out, 150, 0x825940, keep=keep,
                  axes=(lambda c: c[1], lambda c: c[4], lambda c: str(c[3]), lifecycle))


# ======================================================================
# Route beat 09 (fence door) and native fail-stop
# ======================================================================

def check_route_door():
    """Replay 001BC240 / 001BC290 over the door rows of route beat 09.
    Each frame t whose previous row shows the door (node r0) in phase 4 or 5
    runs the native leaf with 001C64F0 answered by row t's +0x1FE (derived
    from the capture, not a claim) and D_008106B8 = row t's request byte B8
    (the room loader clears it earlier in the same frame: the capture shows
    B8 and the phase reset in the same row). Asserted: exactly one 001BC240
    commit, then 001BC290 returns 1 exactly on the row where the capture
    resets the phase to 0, and its +0x0B equals that row's armed byte.
    The beat-09 snapshot also holds the door open script's sound word
    (0x24DC58) that 001BBD60 patched when the door opened: the native
    001BBD60 over the door's +0x56 and side must produce it."""
    import json
    trace = json.loads((ROUTE / '09_fence_door/trace.json').read_text())
    rows = trace['rows']
    head = lambda r: bytes.fromhex(r['door_r0']['h'])
    block = lambda r: bytes.fromhex(r['door_r0']['s1F0'])
    lib = CONTEXT['lib']
    commits = restarts = frames = 0
    for t in range(1, len(rows)):
        prev, cur = rows[t - 1], rows[t]
        phase = head(prev)[5]
        if phase not in (4, 5):
            continue
        frames += 1
        flags = s16(int.from_bytes(block(cur)[14:16], 'little'))
        calls, keep = [], []
        workers = sdf_workers(Env(w_001C64F0=flags), calls, {}, {}, keep)
        door = DoorStep(head(prev)[11], s16(int.from_bytes(block(prev)[14:16], 'little')))
        fault = Fault()
        if phase == 4:
            assert lib.em_sdf_001BC240(C.byref(door), C.byref(workers), C.byref(fault)) == 0
            assert calls == [('w_001C64F0', bits(1.0)), ('w_001BC150',)], calls
            commits += 1
        else:
            b8 = C.c_uint8(bytes.fromhex(cur['req'])[8])
            world, cells = world_from(new_oracle())
            world.d8106B8 = C.pointer(b8)
            result = lib.em_sdf_001BC290(C.byref(door), C.byref(world), C.byref(workers),
                                         C.byref(fault))
            assert (result == 1) == (head(cur)[5] == 0), (t, result, head(cur)[5])
            if result == 1:
                assert door.b0B == head(cur)[11] and \
                    ('w_001C67E0', 0, 0, 0) in calls, (t, door.b0B, calls)
                restarts += 1
        check_fault(fault, f'route 09 row {t}')
        assert door.anim_flags == flags
    assert (commits, restarts) == (1, 1), (commits, restarts)
    m = CONTEXT['images']['s87/route/09_fence_door/eeMemory.bin'] \
        if 's87/route/09_fence_door/eeMemory.bin' in CONTEXT['images'] else \
        (ROUTE / '09_fence_door/eeMemory.bin').read_bytes()
    elf = CONTEXT['elf']
    rd = lambda a, n=4: int.from_bytes(m[a:a + n], 'little')
    door = 0x7A70B0
    assert rd(door + 0x10) == 0x1BC350
    patched = rd(0x24DC58)
    assert patched != struct.unpack_from('<I', elf, 0x24DC58 - 0x100000 + 0x300)[0]
    # 0x24DC14 == 0x45 is the side-0 patch of 001BBE40, which sets +0x2E = 0 first.
    assert rd(0x24DC14) == 0x45 and rd(door + 0x2E, 2) == 0
    CONTEXT['base_saved'] = CONTEXT['base']; CONTEXT['base'] = m
    try:
        word, calls, keep = C.c_uint32(0), [], []
        workers = sdf_workers(Env(), calls, {}, {}, keep)
        fault = Fault()
        assert lib.em_sdf_001BBD60(s16(rd(door + 0x56, 2)), 0, C.byref(word), C.byref(workers),
                                   C.byref(fault)) == 0
    finally:
        CONTEXT['base'] = CONTEXT.pop('base_saved')
    assert word.value == patched, (hex(word.value), hex(patched))
    return frames + 1


def check_fail_stop():
    """Missing workers/data and bad images fault at the original address
    before the step they guard; a latched fault stops later calls."""
    lib, n = CONTEXT['lib'], 0
    calls, keep = [], []
    empty = SdfWorkers()
    fault = Fault()
    door = DoorStep(4, 0)
    assert lib.em_sdf_001BC240(C.byref(door), C.byref(empty), C.byref(fault)) == -1
    assert (fault.address, fault.code, door.anim_flags) == (0x1C64F0, 1, 0); n += 1
    assert lib.em_sdf_001BC240(C.byref(door), C.byref(empty), C.byref(fault)) == -1
    assert fault.address == 0x1C64F0; n += 1          # latched
    workers = sdf_workers(Env(w_001AFA90=[NODES]), calls, {}, {NODES: Spawned()}, keep)
    record = (C.c_ubyte * 0x40)(); struct.pack_into('<I', record, 0x14, 0x01D10800)
    data = bytes(0x10)
    buf = (C.c_ubyte * len(data)).from_buffer_copy(data)
    image = Image(buf, 0x01D10800, len(data))
    owner, fault = SpawnOwner(OWNER, 7), Fault()
    assert lib.em_sdf_001BAC00(C.byref(owner), record, C.byref(image), C.byref(workers),
                               C.byref(fault)) == -1
    assert (fault.address, fault.code, owner.s2E, calls) == (0x01D10800, 3, 7, []); n += 1
    obj, fault = EventActor(), Fault()
    world, cells = world_from(new_oracle())
    world.d275BCC = C.pointer(C.c_int16(0x100))
    workers = sdf_workers(Env(w_001C6150=0x79), calls, {}, {}, keep)
    ev = (C.c_ubyte * 0x2C).from_buffer_copy(event_entry(0x47, 1, 1, 7, 0))
    assert lib.em_sdf_001BAD40(C.byref(obj), ev, C.byref(world), C.byref(workers),
                               C.byref(fault)) == -1
    assert (fault.code, obj.b0C) == (4, 0); n += 1    # bone overflow faults before +0x0C
    for kind in ('creature', 'partner', 'manager'):
        h = seeded_husk(kind, [(0x04, 1, 1 if kind == 'manager' else 0x64)], Env())
        h.workers.w_001B17A0 = type(h.workers.w_001B17A0)()
        fn = {'creature': lib.em_husk_creature_tick, 'partner': lib.em_husk_partner_tick,
              'manager': lib.em_husk_manager_tick}[kind]
        args = (C.byref(h.v),) + ((C.byref(h.world),) if kind != 'partner' else ()) + \
            (C.byref(h.workers), C.byref(h.fault))
        if kind == 'partner': h.v.lifecycle = 1
        assert fn(*args) == -1 and h.fault.address == 0x1B17A0 and h.fault.code == 1, kind
        n += 1
    return n


# ======================================================================
# Main
# ======================================================================

CAPTURES = {'playable': REF / 'playable_ee.bin', 'opening': REF / 'opening_ee.bin'}


def load_images():
    images = {name: (path.read_bytes()) for name, path in CAPTURES.items()}
    extra = sorted(p for p in REF.rglob('eeMemory.bin')) + sorted(REF.rglob('*/*_ee.bin'))
    route = sorted(ROUTE.glob('*/eeMemory.bin'))
    candidates = [(str(p.relative_to(DECOMP)), p) for p in extra + route]
    if not FULL:
        wanted = ('02_elevator', '09_fence', '14_roger')
        candidates = [c for c in candidates if any(w in c[0] for w in wanted)]
    for name, path in candidates:
        images[name] = path.read_bytes()
    return images


def check_overlay_bytes():
    data = AREA11_BIN.read_bytes()
    for name, (start, end) in RANGES.items():
        if start < ARENA: continue
        for image_name, image in CONTEXT['images'].items():
            assert image[start:end] == data[start - ARENA:end - ARENA], (
                'captured overlay code differs from AREA11.BIN', name, image_name)


def branch_report(outcomes):
    missing = []
    for name, (start, end) in RANGES.items():
        for pc in range(start, end, 4):
            word = int.from_bytes(bytes(CONTEXT['base'][pc:pc + 4]), 'little')
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            conditional = (op in (4, 5, 20, 21) and not (op == 4 and rs == rt)) or \
                op in (6, 7, 22, 23) or op == 1 or (op == 17 and rs == 8)
            if not conditional: continue
            seen = {taken for p, taken in outcomes if p == pc}
            if seen != {True, False} and pc not in BRANCH_EXCEPTIONS:
                missing.append((name, hex(pc), sorted(seen)))
    return missing


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA, 'unexpected boot ELF'
    CONTEXT['elf'] = elf
    CONTEXT['images'] = load_images()
    CONTEXT['base'] = CONTEXT['images']['playable']
    CONTEXT['lib'] = build()
    check_overlay_bytes()
    check_bbd60_table()

    summary = {}
    summary['part1'] = part1()

    counts, outcomes, fetched = {}, set(), set()

    def run(name, fn, cases, size=len):
        results = parallel_map(fn, cases)
        for r in results: outcomes.update(r)
        counts[name] = sum(size(c) if size is not len else 1 for c in cases) if size is not len \
            else len(cases)
    run('001BA510', case_ba510, list(range(pick(40, 6))))
    bac = bac00_cases()
    run('001BAC00', case_bac00, bac)
    bad = bad40_cases()
    run('001BAD40', case_bad40, bad)
    run('001B1B30', case_b1b30, b1b30_cases())
    run('001BC240/290', case_door, door_cases())
    run('001BBD60', case_bbd60, bbd60_cases(), size=len)
    run('001B0080', case_b0080, b0080_cases())
    counts['001BBD60'] = sum(len(c) for c in bbd60_cases())
    counts['opening capture fields'] = check_opening_capture()

    captured = [(name, kind, node, entry) for name in CONTEXT['images'] if name != 'opening'
                for kind, node, entry in HUSK_NODES]
    cap_results = parallel_map(husk_capture_case, captured)
    states = {}
    for name, kind, state, outcome, oc, fe in cap_results:
        states.setdefault(kind, set()).add(state)
        outcomes |= oc; fetched |= fe
        assert outcome == 'ok', (name, kind, outcome)
    units = husk_unit_cases()
    unit_results = parallel_map(husk_unit_case, units)
    ticks = 0
    untranslated = set()
    for label, results, oc, fe in unit_results:
        ticks += len(results)
        outcomes |= oc; fetched |= fe
        if results and results[-1] == 'untranslated': untranslated.add(label)
    missing = branch_report(outcomes)
    assert not missing, ('conditional branches not observed both ways', missing)
    counts['route 09 door frames'] = check_route_door()
    counts['native fail-stop checks'] = check_fail_stop()
    print('part 2:', ', '.join(f'{k} {v}' for k, v in counts.items()), 'cases/fields match')
    print('part 3: captured states',
          {k: sorted((f'lc {a:#x} phase {b} flag {c}' for a, b, c in v)) for k, v in states.items()},
          f'over {len(CONTEXT["images"]) - 1} RAM images;',
          f'{len(units)} unit/lockstep cases, {ticks} ticks;',
          f'{len(untranslated)} cases reached untranslated lifecycles 1/4 in both')
    print('branch coverage: every conditional branch of the translated ranges observed both ways')
    banner(f'{len(bac)} 001BAC00, {len(bad)} 001BAD40 cases', f'{len(units)} husk cases',
           f'{len(CONTEXT["images"])} RAM images')
    print('script-door-fan translations match the original instructions')


if __name__ == '__main__':
    main()
