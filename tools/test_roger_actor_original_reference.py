#!/usr/bin/env python3
"""Execute the original Roger actor routines and compare em_roger_actor_original.c.

docs/ROGER_ACTOR_ORIGINAL.md. The user's pinned ELF, the AREA11 overlay file
and captured original RAM supply every instruction, table and resource; none
are embedded here. Routines executed unmodified (instructions copied from the
ELF / overlay file over the captured RAM, and checked equal to it):

  008237E0 lifecycle-0 case     001BA1C0  001B10B0  001AF780  001AF890
  001C6150  001CA6E0  001CA5E0  001CA5F0  001CA6F0  001BA8E0  001CA700
  001D0690  001D06D0  001D06E0  001D0C70  001D8BF0  001BA580  001BA540
  001CA770  001C5C90  00102958  001026A0  001028D0  00102760

Every other callee is hooked, scripted per case and recorded (never simulated
as a claim about the callee); the native module gets the same script through
its workers. The test asserts that the hooked set is exactly the set of call
targets of these routines, that every byte the original stores lies in a
compared field, and that every conditional branch of the translated routines
runs both ways (the few that cannot are listed with the reason).

Every case runs twice, both compared in full: on its own bytes, then with
every byte the original wrote before reading it set to a value the original
never stores there (run_case / RogerEE.perturbation). The original's result
must not change (those bytes are dead inputs to it), and a native store that
is missing, misplaced or wrong now fails even where the capture already held
the stored value. The test then asserts, from a byte-level data-flow record
of the original's stores, that every store instance whose value is used
(read later or left at the end) changes a byte in some run, so no store is
invisible to the comparison (RogerEE `store visibility`; STORE_EXEMPT and
DEAD_STORES list the identical rewrites and always-overwritten stores).

--mutants runs MUTANTS (single edits of the module, each deleting or
changing one original store) and asserts that the default run fails for
every one.

Arithmetic: COP1 and VU0 macro operations run through tools/ee_float_model.py
(FallEE from tools/test_player_fall_reference.py, subclassed here; no shared
file is edited).

Default run (~6 s CPU): every case class and boundary plus a fixed-seed sample;
the route beats 00, 10 and 14 plus the playable capture. EM_TEST_FULL=1: the
exhaustive sweep and all 19 captures (the 15 route beats and the four
startup-reference images).
"""
import ctypes as C
import hashlib
import json
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
from test_player_fall_reference import FallEE  # noqa: E402
from test_player_slide_reference import RETURN, DECOMP, ELF_SHA256, bits, sx32  # noqa: E402

LANE = os.environ.get('EM_LANE', 'roger_actor_original_reference')
OUT = ROOT / 'build' / LANE
REFERENCE = DECOMP / 'build/startup-reference'
ROUTE = DECOMP / 'build/s87/route'

ROGER, EQUIP = 0x7A8830, 0x7A8B20          # pool records in every capture
OVERLAY_BASE = 0x823500
CONTROLLER, CASE0, CASE0_END = 0x8237E0, 0x823824, 0x8238A0
STACK_LO, SLOTS_LO, SLOTS_HI = 0x7D4640, 0x7D6950, 0x810040
TABLE, TABLE_WORDS = 0x28A490, 0x100
BCC, BD0, B40 = 0x275BCC, 0x275BD0, 0x275B40
ACTIVITY, SPAD = 0x8106D4, 0x70003600
D758, D788, D700 = 0x810758, 0x810788, 0x810700
DRAW = 0x1CAA00
EM_SLOT = 0xD0                             # bone / face slot size (001AF890 clears 13 qw)

# Translated routines (start: size, from the splat listing of each).
SIZES = {0x1BA1C0: 0x30, 0x1B10B0: 0xE0, 0x1AF780: 0x38, 0x1AF890: 0x44, 0x1C6150: 0x8,
         0x1CA6E0: 0x8, 0x1CA5E0: 0xC, 0x1CA5F0: 0xEC, 0x1CA6F0: 0x8, 0x1BA8E0: 0x320,
         0x1CA700: 0x6C, 0x1D0690: 0x40, 0x1D06D0: 0xC, 0x1D06E0: 0x38, 0x1D0C70: 0x8,
         0x1D8BF0: 0x2C, 0x1BA580: 0x268, 0x1BA540: 0x3C, 0x1CA770: 0x40, 0x1C5C90: 0x318,
         0x102958: 0x24, 0x1026A0: 0x2C, 0x1028D0: 0x14, 0x102760: 0x34}
# Hooked callees (call or tail-call targets outside the translation).
HOOKED = {0x1C63E0: 'bone_init_default_2', 0x1CB5B0: 'anim_bone_array_setup',
          0x1F0120: '001F0120', 0x1DA6A0: '001DA6A0', 0x1BA7F0: '001BA7F0',
          0x1D0720: '001D0720', 0x1B1020: '001B1020', 0x1AFC10: '001AFC10'}
# 001CA5F0's other twelve handlers are reached only with a nonzero kind,
# which 001CA6E0 never passes (0 is hard-wired in its delay slot).
# Stores that rewrite, by construction, the value the same bytes already
# hold on every path (so deleting one is an equivalent mutant; the test
# asserts each is never visible AND always rewrites the same value).
STORE_EXEMPT = {
    # 001C5C90's second scratch set-up (5.4, 1.0, 0, 1.0) rewrites words
    # 70003604/08/0C with the 1.0, 0 and 1.0 its first set-up stored there;
    # 001026A0 in between only reads the scratch.
    0x1C5EC8: 'scratch +4 = 1.0 again', 0x1C5ED0: 'scratch +8 = 0 again',
    0x1C5ED8: 'scratch +0xC = 1.0 again',
}
# Stores always overwritten before anything reads them (deleting one is
# an equivalent mutant; the test asserts each runs and is never live).
DEAD_STORES = {
    0x1C5F38: '+0xAC = va.w, then +0xAC = 1.0', 0x1C5F5C: '+0xBC = vb.w, then +0xBC = 1.0',
}
UNREACHED_BRANCHES = {
    # 001CA5F0: the table range check (kind 0 is always in range).
    'kind 0 only': (0x1CA5F0, 0x1CA5F0 + 0xEC),
}


# ======================================================================
# The interpreter
# ======================================================================

class RogerEE(FallEE):
    """FallEE (measured float model) over one captured RAM image, with an
    undo log so every case starts from the same bytes, a record of every
    stored address and of the conditional-branch outcomes, and a byte-level
    data-flow record of every store the translated instructions make (see
    `store visibility` below)."""

    def __init__(self, elf, ram, spad):
        super().__init__(elf, ram, spad)
        self.undo = {}
        self.written = set()
        self.outcomes = set()
        self.tracking = self.in_exec = False
        self.live, self.visible = set(), set()     # store keys, over every run
        self.store_pcs = set()                     # store sites executed, over every run
        self.reset_flow()

    # ---- store visibility ---------------------------------------------
    # A store key is (pc, n): the n-th execution, within one run, of the
    # store instruction at pc. A byte it stored is LIVE when a later
    # instruction reads it or it is still there when the run ends (a later
    # store to the byte first makes it dead). The key is VISIBLE in a run
    # when one of its live bytes held a different value just before the
    # store: only then does deleting that store (or putting the value
    # elsewhere) change a byte the test compares or a value the original
    # goes on to use. The test asserts every key live in some run is
    # visible in some run (STORE_EXEMPT lists the stores that rewrite an
    # identical value by construction).
    def reset_flow(self):
        self.first = {}       # byte -> 'r' / 'w': the run's first access
        self.stored = {}      # byte -> every byte value the run stored there
        self.pending = {}     # byte -> (key, changed) of the last store
        self.occ = {}
        self.store_base = None

    def finish_flow(self):
        for key, changed in self.pending.values():
            self.live.add(key)
            if changed:
                self.visible.add(key)
        self.pending = {}

    def execute(self, word, pc):
        if not self.tracking:
            return super().execute(word, pc)
        self.in_exec, self.cur_pc, self.cur_word, self.store_base = True, pc, word, None
        try:
            return super().execute(word, pc)
        finally:
            self.in_exec = False

    def load(self, address, size=4):
        if self.in_exec:
            a = address & 0xFFFFFFFF
            for b in range(a, a + size):
                if b < 0x7F000000 and b not in self.first:
                    self.first[b] = 'r'
                p = self.pending.get(b)
                if p is not None:
                    self.live.add(p[0])
                    if p[1]:
                        self.visible.add(p[0])
        return super().load(address, size)

    def save(self, address, value, size=4):
        a = address & 0xFFFFFFFF
        # Data-flow over every store but the register saves (base $sp);
        # the stack bytes a routine hands its caller (001026A0's result
        # vector) count, through the caller's later read.
        if self.in_exec and self.cur_word >> 21 & 31 != 29:
            if self.store_base is None:
                self.store_base = a
                self.store_pcs.add(self.cur_pc)
                self.cur_occ = self.occ.get(self.cur_pc, 0)
                self.occ[self.cur_pc] = self.cur_occ + 1
            for i in range(size):
                b = a + i
                new = value >> (8 * i) & 0xFF
                if b < 0x7F000000:
                    if b not in self.first:
                        self.first[b] = 'w'
                    self.stored.setdefault(b, set()).add(new)
                self.pending[b] = ((self.cur_pc, self.cur_occ), FallEE.load(self, b, 1) != new)
        if a < 0x7F000000:
            for i in range(size):
                if a + i not in self.undo:
                    self.undo[a + i] = FallEE.load(self, a + i, 1)
                self.written.add(a + i)
        super().save(address, value, size)

    def write(self, address, data):
        for i, byte in enumerate(data):
            self.save(address + i, byte, 1)

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and (any(s <= pc < s + n for s, n in SIZES.items())
                              or CASE0 <= pc < CASE0_END):
            self.outcomes.add((pc, b[0]))
        return b

    def restore(self):
        for a, v in self.undo.items():
            FallEE.save(self, a, v, 1)
        self.undo.clear()
        self.written = set()
        self.r = [0] * 32
        self.rh = [0] * 32
        self.f = [0] * 32
        self.vf = [[0, 0, 0, 0] for _ in range(32)]
        self.vf[0][3] = bits(1.0)
        self.vacc = [0, 0, 0, 0]
        self.q = 0
        self.acc = 0
        self.cond = False
        self.hi = self.lo = 0
        self.r[28] = 0x27D370
        self.r[29] = 0x7F0F0000

    def run_routine(self, entry, args):
        for i, value in enumerate(args):
            self.r[4 + i] = sx32(value)
        self.r[31] = RETURN
        self.reset_flow()
        self.tracking = True
        try:
            self.run(entry)
        finally:
            self.tracking = self.in_exec = False
        self.finish_flow()

    def perturbation(self):
        """Pokes giving every byte the last run wrote BEFORE reading a value
        it never stored there (so != the value it leaves). The run never
        observed those bytes' old values, so the original's result cannot
        depend on them (asserted by rerunning it); the native side, which
        starts from the same poked bytes, now fails on any store it omits,
        misplaces or gets wrong, even where the capture already held the
        stored value."""
        pokes = []
        for b, kind in self.first.items():
            if kind != 'w':
                continue
            used = self.stored[b]
            v = (b * 0x3B + 0x5A) & 0xFF
            while v in used:
                v = (v + 0x61) & 0xFF
            pokes.append((b, v, 1))
        return pokes


def load_image(path, elf, overlay):
    """Captured RAM with the executed routines' bytes taken from the files
    (asserted equal to what the capture holds)."""
    ram = bytearray(path.read_bytes())
    assert len(ram) == 0x2000000
    for start, size in SIZES.items():
        code = elf[start - 0x100000 + 0x300:start - 0x100000 + 0x300 + size]
        assert ram[start:start + size] == code, ('capture code differs from the ELF', hex(start))
    code = overlay[CONTROLLER - OVERLAY_BASE:0x823910 - OVERLAY_BASE]
    assert ram[CONTROLLER:0x823910] == code, 'capture overlay differs from AREA11.BIN'
    return ram


def check_callee_set(ee):
    """Every call / tail-call target of the translated routines (and of the
    lifecycle-0 case) is translated or hooked; every hook is a real target."""
    targets = set()
    ranges = list(SIZES.items()) + [(CASE0, CASE0_END - CASE0)]
    for start, size in ranges:
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            if word >> 26 in (2, 3):
                targets.add((pc & 0xF0000000) | (word & 0x3FFFFFF) << 2)
    missing = sorted(t for t in targets if t not in SIZES and t not in HOOKED)
    assert not missing, ('callees neither hooked nor translated', [hex(t) for t in missing])
    unused = sorted(t for t in HOOKED if t not in targets)
    assert not unused, ('hooks no translated routine calls', [hex(t) for t in unused])
    return len(targets)


STORE_OPS = {40: 1, 41: 2, 43: 4, 63: 8, 31: 16, 57: 4, 62: 16}   # sb sh sw sd sq swc1 sqc2


def store_sites(ee):
    """Every store instruction of the translated routines (stack stores,
    which are not compared, are dropped when the run records them)."""
    sites = {}
    for start, size in list(SIZES.items()) + [(CASE0, CASE0_END - CASE0)]:
        for pc in range(start, start + size, 4):
            op = ee.load(pc) >> 26
            if op in STORE_OPS and ee.load(pc) >> 21 & 31 != 29:
                sites[pc] = STORE_OPS[op]
    return sites


def check_store_visibility(ee_list, sites):
    """Every live store key is visible in some run (RogerEE); every store
    site outside the stack is executed live."""
    live = set().union(*(e.live for e in ee_list))
    visible = set().union(*(e.visible for e in ee_list))
    executed = set().union(*(e.store_pcs for e in ee_list))
    invisible = sorted(k for k in live - visible if k[0] not in STORE_EXEMPT)
    assert not invisible, ('stores whose old value always equals the stored one',
                           [f'{pc:06X}#{n}' for pc, n in invisible[:24]])
    exempt_seen = sorted(k for k in visible if k[0] in STORE_EXEMPT)
    assert not exempt_seen, ('an exempt store changed a byte: drop its exemption',
                             [f'{pc:06X}#{n}' for pc, n in exempt_seen])
    live_pcs = {k[0] for k in live}
    dead_live = sorted(pc for pc in DEAD_STORES if pc in live_pcs or pc not in executed)
    assert not dead_live, ('a listed dead store was live or never ran: drop it', [hex(pc) for pc in dead_live])
    unreached = {pc for pc in sites for lo, hi in UNREACHED_BRANCHES.values()
                 if lo <= pc < hi and pc not in executed}
    unexecuted = sorted(pc for pc in sites if pc not in live_pcs and pc not in DEAD_STORES
                        and pc not in unreached)
    assert not unexecuted, ('store sites never executed live', [hex(pc) for pc in unexecuted])
    return len(sites), len(unreached), len(live)


def branch_sites(ee):
    sites = set()
    for start, size in list(SIZES.items()) + [(CASE0, CASE0_END - CASE0)]:
        for pc in range(start, start + size, 4):
            word = ee.load(pc)
            op, rt = word >> 26, word >> 16 & 31
            if op in (4, 5, 6, 7, 20, 21, 22, 23) or (op == 1 and rt in (0, 1, 2, 3)) or \
               (op == 17 and word >> 21 & 31 == 8):
                if op == 4 and word >> 16 & 0x3FF == 0:   # beq zero, zero: unconditional
                    continue
                sites.add(pc)
    return sites


# ======================================================================
# Native side
# ======================================================================

U8, U16, I16, U32, I32 = C.c_uint8, C.c_uint16, C.c_int16, C.c_uint32, C.c_int32


class Rec(C.Structure):
    _fields_ = [('address', U32), ('status', U8), ('drawn', U8), ('cls', U8), ('lifecycle', U8),
                ('bones_held', U8), ('bone_count', U8), ('kind', U8), ('w14', U32),
                ('parent', U32), ('descriptor', U32), ('anim', U32), ('model', U32),
                ('draw', U32), ('face_active', I16), ('w58', U32), ('face', U32),
                ('face_bone', I16), ('shadow_kind', I16), ('pose_bone', U8),
                ('fA0', U32 * 4), ('fB0', U32 * 4), ('fC0', U32 * 4), ('bone', U32 * 56)]


# (field, offset, size, signed) of every modelled record byte.
FIELDS = [('status', 0x00, 1), ('drawn', 0x01, 1), ('cls', 0x02, 1), ('lifecycle', 0x04, 1),
          ('bones_held', 0x09, 1), ('bone_count', 0x0C, 1), ('kind', 0x0D, 1), ('w14', 0x14, 4),
          ('parent', 0x18, 4), ('descriptor', 0x30, 4), ('anim', 0x40, 4), ('model', 0x44, 4),
          ('draw', 0x4C, 4), ('face_active', 0x56, 2), ('w58', 0x58, 4), ('face', 0x90, 4),
          ('face_bone', 0x94, 2), ('shadow_kind', 0x96, 2), ('pose_bone', 0x98, 1)]
ARRAYS = [('fA0', 0xA0, 4), ('fB0', 0xB0, 4), ('fC0', 0xC0, 4), ('bone', 0x110, 56)]
COVERED = set()
for _, off, size in FIELDS:
    COVERED.update(range(off, off + size))
for _, off, count in ARRAYS:
    COVERED.update(range(off, off + 4 * count))

RESOURCE = C.CFUNCTYPE(C.c_void_p, C.c_void_p, U32, U32)


class World(C.Structure):
    _fields_ = [('d00275BCC', C.POINTER(I16)), ('d00275BD0', C.POINTER(U32)),
                ('slot_stack', C.POINTER(U32)), ('slot_stack_base', U32), ('slot_stack_words', U32),
                ('slots', C.POINTER(U8)), ('slots_base', U32), ('slots_size', U32),
                ('d0028A490', C.POINTER(U32)), ('d0028A490_count', U32),
                ('d00810758', C.POINTER(U8)), ('d00810788', C.POINTER(U8)),
                ('d00810700', C.POINTER(U8)), ('d008106D4', C.POINTER(U8)),
                ('d00275B40', C.POINTER(U32)), ('d00275B40_count', U32),
                ('spad3600', C.POINTER(U32)), ('resource', RESOURCE), ('resource_ctx', C.c_void_p)]


RP = C.POINTER(Rec)
W_CLIP = C.CFUNCTYPE(C.c_int, C.c_void_p, RP, I32)
W_COUNT = C.CFUNCTYPE(C.c_int, C.c_void_p, U8)
W_SPAWN = C.CFUNCTYPE(C.c_int, C.c_void_p, U32, I32)
W_ACTOR = C.CFUNCTYPE(C.c_int, C.c_void_p, RP)
W_BIND = C.CFUNCTYPE(C.c_int, C.c_void_p, RP, U32, I32, I32, C.POINTER(I32))


class Workers(C.Structure):
    _fields_ = [('ctx', C.c_void_p), ('w_001C63E0', W_CLIP), ('w_001CB5B0', W_COUNT),
                ('w_001F0120', W_SPAWN), ('w_001DA6A0', W_ACTOR), ('w_001BA7F0', W_ACTOR),
                ('w_001D0720', W_ACTOR), ('w_001B1020', W_BIND), ('w_draw', W_ACTOR),
                ('w_001AFC10', W_ACTOR)]


class Fault(C.Structure):
    _fields_ = [('address', U32), ('code', I32)]


class State(C.Structure):
    _fields_ = [('world', World), ('workers', Workers), ('fault', Fault)]


SOURCE = 'src/game/em_roger_actor_original.c'

# --mutants: single edits of the module, each of which must make the
# default run fail. Each deletes or changes one store (or one condition in
# front of stores) that the original makes; the first group are the stores
# no case could see before write-first bytes were perturbed (the six a
# review found, the +0x56 = 1 of 001BA8E0 and the first scratch set-up).
MUTANTS = [
    ('init +0x00 = 1 dropped', '    roger->status = 1; ', '    '),
    ('init +0x30 dropped', '    roger->descriptor = EM_ROGER_ACTOR_DESCRIPTOR;', ''),
    ('init +0x58 dropped', '    roger->w58 = w58; ', '    '),
    ('init +0x98 = 2 dropped', '    roger->pose_bone = EM_ROGER_ACTOR_POSE_BONE;', ''),
    ('001CA5F0 +0x4C dropped', '    a->draw = EM_ROGER_ACTOR_DRAW_001CAA00;\n', ''),
    ('001C5C90 +0x01 = 1 dropped', '    e->drawn = 1; ', '    '),
    ('001BA8E0 +0x56 = 1 dropped', '        a->face_active = 1; ', '        '),
    ('001C5C90 first scratch +4 dropped', 'spad[0] = 0; spad[1] = F_ONE; spad[2]', 'spad[0] = 0; spad[2]'),
    ('001D0690 +0x80 clear dropped', '    r[0x10] = 0;\n', ''),
    ('001D0690 +0x8C clear dropped', '    put32(r + 0x1C, 0);\n', ''),
    ('001D0690 +0x84 clear dropped', '    put32(r + 0x14, 0);\n', ''),
    ('001D0690 five target words', 'for (int i = 0; i < 6; ++i) put32(r + 0x20', 'for (int i = 0; i < 5; ++i) put32(r + 0x20'),
    ('001D06E0 always clears', '    if (a1 == 0)  ', '    if (1)  '),
    ('001D06E0 never clears', '    if (a1 == 0)  ', '    if (0)  '),
    ('0x4E no longer copies', 'case 0x47: case 0x4E:', 'case 0x47:'),
    ('001B10B0 +0x0C dropped', '    a->bone_count = count;\n', ''),
    ('001AF890 stack word dropped', '    *word = slot;\n', ''),
    ('001CA700 +0x94 dropped', '    a->face_bone = (int16_t)a2; ', '    '),
    ('001C5C90 +0xC0 = vb - va dropped', '    memcpy(e->fC0, dir, sizeof dir);\n    if (sdk_00102760',
     '    if (sdk_00102760'),
    ('001BA580 activity 1 not consumed', 'em_roger_actor_001D06E0(s, a, 1) < 0) return -1;\n        *activity = 0;',
     'em_roger_actor_001D06E0(s, a, 1) < 0) return -1;'),
]


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    source, lib = SOURCE, OUT / f'roger_actor.{ext}'
    mutant = os.environ.get('EM_ROGER_MUTANT')
    if mutant is not None:
        name, before, after = MUTANTS[int(mutant)]
        text = (ROOT / SOURCE).read_text()
        assert text.count(before) == 1, ('mutant edit does not apply exactly once', name)
        source = OUT / f'mutant_{mutant}.c'
        source.write_text(text.replace(before, after))
        lib = OUT / f'roger_actor_mutant_{mutant}.{ext}'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', str(source), '-o', str(lib)]
                   + ([] if mutant is not None else ['-Werror']), cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    S = C.POINTER(State)
    native.em_roger_actor_008237E0_init.argtypes = [S, RP]
    native.em_roger_actor_001BA8E0.argtypes = [S, RP, U32]
    native.em_roger_actor_001BA580.argtypes = [S, RP, U32]
    native.em_roger_actor_001BA540.argtypes = [S, RP]
    native.em_roger_actor_001C5C90.argtypes = [S, RP, RP]
    native.em_roger_actor_001B10B0.argtypes = [S, RP, U32, I32]
    native.em_roger_actor_001AF780.argtypes = [S, C.POINTER(U32)]
    native.em_roger_actor_001AF890.argtypes = [S, U32]
    native.em_roger_actor_001BA1C0.argtypes = [S, U32]
    native.em_roger_actor_001D0690.argtypes = [S, U32]
    native.em_roger_actor_001D06D0.argtypes = [S, RP, U32]
    native.em_roger_actor_001D06E0.argtypes = [S, RP, U32]
    return native


def record_from(ee, address):
    rec = Rec()
    rec.address = address
    for name, off, size in FIELDS:
        value = ee.load(address + off, size)
        if name in ('face_active', 'face_bone', 'shadow_kind') and value & 0x8000:
            value -= 0x10000
        setattr(rec, name, value)
    for name, off, count in ARRAYS:
        arr = getattr(rec, name)
        for i in range(count):
            arr[i] = ee.load(address + off + 4 * i)
    return rec


def record_bytes(rec):
    """The modelled record bytes as {offset: byte}."""
    out = {}
    for name, off, size in FIELDS:
        value = getattr(rec, name) & ((1 << (8 * size)) - 1)
        for i in range(size):
            out[off + i] = value >> (8 * i) & 255
    for name, off, count in ARRAYS:
        arr = getattr(rec, name)
        for i in range(count):
            for j in range(4):
                out[off + 4 * i + j] = arr[i] >> (8 * j) & 255
    return out


class Native:
    """Native world views built from the same bytes the original starts from,
    and workers that follow the same script and record the same calls."""

    def __init__(self, lib, ee, script, resource_buffer):
        self.lib, self.script, self.calls = lib, script, []
        self.bcc = I16(struct.unpack('<h', ee.read(BCC, 2))[0])
        self.bd0 = U32(ee.load(BD0))
        words = (SLOTS_LO - STACK_LO) // 4
        self.stack = (U32 * words).from_buffer_copy(ee.read(STACK_LO, SLOTS_LO - STACK_LO))
        self.slots = (U8 * (SLOTS_HI - SLOTS_LO)).from_buffer_copy(ee.read(SLOTS_LO, SLOTS_HI - SLOTS_LO))
        self.table = (U32 * TABLE_WORDS).from_buffer_copy(ee.read(TABLE, 4 * TABLE_WORDS))
        self.d758 = (U8 * 1)(ee.load(D758, 1))
        self.d788 = (U8 * 1)(ee.load(D788, 1))
        self.d700 = (U8 * 1)(ee.load(D700, 1))
        self.activity = (U8 * 10).from_buffer_copy(ee.read(ACTIVITY, 10))
        self.b40 = (U32 * 4)(*[ee.load(script.get('b40', 0) + 4 * i) for i in range(4)])
        self.spad = (U32 * 4).from_buffer_copy(ee.read(SPAD, 16))
        base = C.addressof(resource_buffer)

        def resource(_, address, size):
            return base + address if address + size <= 0x2000000 else None
        self.cb = [RESOURCE(resource)]
        w = World()
        w.d00275BCC, w.d00275BD0 = C.pointer(self.bcc), C.pointer(self.bd0)
        w.slot_stack, w.slot_stack_base, w.slot_stack_words = self.stack, STACK_LO, words
        w.slots, w.slots_base, w.slots_size = self.slots, SLOTS_LO, SLOTS_HI - SLOTS_LO
        w.d0028A490, w.d0028A490_count = self.table, TABLE_WORDS
        w.d00810758, w.d00810788, w.d00810700 = self.d758, self.d788, self.d700
        w.d008106D4 = self.activity
        w.d00275B40, w.d00275B40_count = self.b40, 4
        w.spad3600 = self.spad
        w.resource, w.resource_ctx = self.cb[0], None
        k = Workers()
        rec = lambda name: (lambda _, a: self.calls.append((name, a.contents.address)) or 0)
        k.w_001C63E0 = self.keep(W_CLIP(lambda _, a, clip: self.calls.append(
            ('bone_init_default_2', a.contents.address, clip)) or 0))
        k.w_001CB5B0 = self.keep(W_COUNT(lambda _, n: self.calls.append(('anim_bone_array_setup', n)) or 0))
        k.w_001F0120 = self.keep(W_SPAWN(lambda _, o, key: self.calls.append(('001F0120', o, key)) or 0))
        k.w_001DA6A0 = self.keep(W_ACTOR(rec('001DA6A0')))
        k.w_001BA7F0 = self.keep(W_ACTOR(rec('001BA7F0')))
        k.w_001D0720 = self.keep(W_ACTOR(rec('001D0720')))
        k.w_draw = self.keep(W_ACTOR(rec('draw')))
        k.w_001AFC10 = self.keep(W_ACTOR(rec('001AFC10')))

        def bind(_, a, a1, a2, a3, result):
            self.calls.append(('001B1020', a.contents.address, a1, a2, a3))
            result[0] = script.get('bind', 0)
            return 0
        k.w_001B1020 = self.keep(W_BIND(bind))
        self.state = State(w, k, Fault())

    def keep(self, fn):
        self.cb.append(fn)
        return fn

    def compare(self, ee, where):
        """Every modelled global compared with the original's bytes."""
        got = {BCC: self.bcc.value & 0xFFFF, BD0: self.bd0.value}
        want = {BCC: ee.load(BCC, 2), BD0: ee.load(BD0)}
        assert got == want, (where, 'slot counters', got, want)
        assert bytes(self.stack) == ee.read(STACK_LO, SLOTS_LO - STACK_LO), (where, 'slot stack')
        if bytes(self.slots) != ee.read(SLOTS_LO, SLOTS_HI - SLOTS_LO):
            a, b = bytes(self.slots), ee.read(SLOTS_LO, SLOTS_HI - SLOTS_LO)
            first = next(i for i in range(len(a)) if a[i] != b[i])
            raise AssertionError((where, 'slot bytes differ at', hex(SLOTS_LO + first),
                                  a[first:first + 16].hex(), b[first:first + 16].hex()))
        assert bytes(self.activity) == ee.read(ACTIVITY, 10), (where, 'activity bytes')
        assert bytes(self.spad) == ee.read(SPAD, 16), (where, 'scratchpad 3600')


def compare_record(ee, rec, address, where):
    got = record_bytes(rec)
    want = {off: ee.load(address + off, 1) for off in got}
    diff = {hex(o): (got[o], want[o]) for o in got if got[o] != want[o]}
    assert not diff, (where, 'record', hex(address), diff)


GLOBAL_RANGES = ((STACK_LO, SLOTS_HI), (BCC, BCC + 2), (BD0, BD0 + 4), (ACTIVITY, ACTIVITY + 10),
                 (SPAD, SPAD + 16))


def assert_writes_covered(ee, records, where):
    stray = sorted(a for a in ee.written
                   if not any(lo <= a < hi for lo, hi in GLOBAL_RANGES)
                   and not any(0 <= a - r < 0x2F0 and a - r in COVERED for r in records))
    assert not stray, (where, 'original stores outside the compared fields', [hex(a) for a in stray[:16]])


# ======================================================================
# Hooks (the original side of the script)
# ======================================================================

def install_hooks(ee, script, calls):
    def rec(name):
        def hook(e):
            calls.append((name, e.arg(0)))
        return hook
    ee.hooks = {
        0x1C63E0: lambda e: calls.append(('bone_init_default_2', e.arg(0), (e.arg(1) ^ 0x8000) - 0x8000)),
        0x1CB5B0: lambda e: calls.append(('anim_bone_array_setup', e.arg(0) & 0xFF)),
        0x1F0120: lambda e: calls.append(('001F0120', e.load(e.arg(0) + 0x14),
                                          (e.arg(1) ^ 0x80000000) - 0x80000000)),
        0x1DA6A0: rec('001DA6A0'), 0x1BA7F0: rec('001BA7F0'), 0x1D0720: rec('001D0720'),
        0x1AFC10: rec('001AFC10'), DRAW: rec('draw'),
    }

    def bind(e):
        a = e.arg(0)
        calls.append(('001B1020', a, e.arg(1), (e.arg(2) ^ 0x80000000) - 0x80000000,
                      (e.arg(3) ^ 0x80000000) - 0x80000000))
        # Only the scripted result: 001B1020's own +0x04 stores are not
        # simulated (001C5D0C overwrites +0x04 with 1 on both sides).
        e.ret_int(script.get('bind', 0))
    ee.hooks[0x1B1020] = bind


# ======================================================================
# Cases
# ======================================================================

def prepare(ee, case):
    """Apply a case's initial bytes to the EE image (logged for undo)."""
    for address, value, size in case.get('pokes', ()):
        ee.save(address, value, size)
    if 'b40' in case:
        ee.save(B40, case['b40'])


def run_case(ee, lib, resources, case):
    """Two passes, both compared in full: the case's own bytes, then the
    same bytes with every byte the original wrote before reading it
    perturbed (RogerEE.perturbation). The second pass must leave the
    original's result, calls and stored bytes unchanged (so the perturbed
    bytes really are dead inputs), and it is where a native store that is
    missing, misplaced or wrong fails although the capture (or an earlier
    store) already held the right value."""
    entry, first = run_pass(ee, lib, resources, case, ())
    pokes = ee.perturbation()
    _, second = run_pass(ee, lib, resources, case, pokes)
    assert second == first, (case['name'], 'perturbing write-first bytes changed the original')
    return entry


def run_pass(ee, lib, resources, case, perturb):
    ee.restore()
    prepare(ee, case)
    for address, value, size in perturb:
        ee.save(address, value, size)
    ee.written = set()
    records = case['records']
    natives = {a: record_from(ee, a) for a in records}
    native = Native(lib, ee, case, resources)
    original_calls = []
    install_hooks(ee, case, original_calls)
    entry, which = case['entry'], case['what']
    if which == 'init':
        ee.run_routine(CONTROLLER, (ROGER,))
        result = lib.em_roger_actor_008237E0_init(C.byref(native.state), C.byref(natives[ROGER]))
        expect = 0
    elif which == 'attach':
        ee.run_routine(0x1BA8E0, (ROGER, case['kind']))
        result = lib.em_roger_actor_001BA8E0(C.byref(native.state), C.byref(natives[ROGER]), case['kind'])
        expect = 0
    elif which == 'update':
        ee.run_routine(0x1BA580, (ROGER, case['kind']))
        result = lib.em_roger_actor_001BA580(C.byref(native.state), C.byref(natives[ROGER]), case['kind'])
        expect = 0
    elif which == 'release':
        ee.run_routine(0x1BA540, (ROGER,))
        result = lib.em_roger_actor_001BA540(C.byref(native.state), C.byref(natives[ROGER]))
        expect = 0
    elif which == 'equipment':
        ee.run_routine(0x1C5C90, (EQUIP,))
        result = lib.em_roger_actor_001C5C90(C.byref(native.state), C.byref(natives[EQUIP]),
                                             C.byref(natives[ROGER]))
        expect = 0 if ('001AFC10', EQUIP) in original_calls else 1
    elif which == 'reset':
        ee.run_routine(0x1D0690, (case['target'],))
        result = lib.em_roger_actor_001D0690(C.byref(native.state), case['target'])
        expect = 0
    elif which in ('speed', 'talk'):
        entry_pc = 0x1D06D0 if which == 'speed' else 0x1D06E0
        fn = lib.em_roger_actor_001D06D0 if which == 'speed' else lib.em_roger_actor_001D06E0
        ee.run_routine(entry_pc, (ROGER, case['a1']))
        result = fn(C.byref(native.state), C.byref(natives[ROGER]), case['a1'])
        expect = 0
    elif which == 'bind':
        ee.run_routine(0x1B10B0, (ROGER, case['a1'], case['a2']))
        result = lib.em_roger_actor_001B10B0(C.byref(native.state), C.byref(natives[ROGER]),
                                             case['a1'], case['a2'])
        expect = ee.r[2] & 0xFFFFFFFF
    else:
        raise AssertionError(which)
    snapshot = (ee.r[2] & 0xFFFFFFFF, tuple(original_calls),
                tuple(sorted((a, ee.load(a, 1)) for a in ee.written)))
    where = dict(case=case['name'], perturbed=len(perturb))
    assert native.state.fault.code == 0, (where, 'native fault', hex(native.state.fault.address),
                                          native.state.fault.code)
    assert result == expect, (where, 'result', result, expect)
    assert native.calls == original_calls, (where, 'calls', native.calls, original_calls)
    for address in records:
        compare_record(ee, natives[address], address, where)
    native.compare(ee, where)
    assert_writes_covered(ee, records, where)
    if 'fixed' in case:
        # The capture already holds this frame's tick result: the native
        # +0xA0..+0xCF must reproduce the captured bytes exactly.
        e = natives[EQUIP]
        got = struct.pack('<12I', *e.fA0, *e.fB0, *e.fC0)
        assert got == case['fixed'], (where, 'captured equipment vectors', got.hex(), case['fixed'].hex())
    return entry, snapshot


def sentinel_word(address):
    """A nonzero word whose four bytes differ from each other and from the
    neighbouring words' bytes, so a missing, extra or misplaced store shows."""
    return int.from_bytes(bytes(((address * 7 + i * 0x35 + 0x5B) & 0xFF) or 0xC3
                                for i in range(4)), 'little')


def sentinel_slot(address, lo=0x00, hi=EM_SLOT):
    """Pokes filling slot bytes [lo, hi) with sentinels. The face routines
    clear or keep bytes the captured face slot and a freshly popped slot
    (001AF890 zeroed it) already hold as 0, so without these their clears
    are invisible (001D0690 +0x80/+0x84/+0x88/+0x8C/+0x90..+0xA7; the
    001D06E0 target clear; the kept +0x74/+0x7C/+0x81)."""
    assert SLOTS_LO <= address and (address - SLOTS_LO) % EM_SLOT == 0 and address + EM_SLOT <= SLOTS_HI, \
        ('not a slot', hex(address))
    return [(address + off, sentinel_word(address + off), 4) for off in range(lo, hi, 4)]


def pop_slots(ee, words):
    """The slot addresses at the stack cursor word + each of `words`, i.e.
    the slots 001AF780 hands out next."""
    cursor = ee.load(BD0)
    return [ee.load(cursor + 4 * w) for w in words]


def unit_cases(ee):
    """Case list over the playable capture (Roger initialised there)."""
    face = ee.load(ROGER + 0x90)
    bones = ee.load(ee.load(TABLE + 4 * 0x47) + 8, 1)
    # Sentinel bytes over the captured face slot and the slots the stack
    # hands out next: word 0 (the attach's lazy pop) and word `bones` (the
    # init's face pop after the 21 bone pops).
    init_seed = sentinel_slot(face) + sum((sentinel_slot(a) for a in pop_slots(ee, (0, bones))), [])
    attach_seed = sentinel_slot(face) + sentinel_slot(pop_slots(ee, (0,))[0])
    face_seed = sentinel_slot(face)
    cases = []
    # 008237E0 lifecycle 0: the 758 gate, 788 gate, an existing face slot,
    # and the free-slot count around the bone cap (21) and 001AF780's 31.
    for d758 in (0, 0xFF):
        for d788 in (0, 1):
            for has_face in (0, 1):
                for bcc in (0, 20, 21, 30, 31, 32, 40, 51, 52, 1035):
                    pokes = init_seed + [(ROGER + 4, 0, 1), (D758, d758, 1), (D788, d788, 1),
                                         (ROGER + 0x90, face if has_face else 0, 4), (BCC, bcc, 2)]
                    cases.append(dict(name=f'init 758={d758} 788={d788} face={has_face} bcc={bcc}',
                                      what='init', entry='008237E0', records=(ROGER,), pokes=pokes,
                                      axes=(d758, d788, has_face, bcc), keep=d758 == 0 and d788 == 0))
    # 001BA8E0 over every kind byte.
    for kind in range(256):
        for d788 in (0, 1):
            for has_face in (0, 1):
                for bcc in (30, 31, 1035):
                    pokes = attach_seed + [(D788, d788, 1), (ROGER + 0x90, face if has_face else 0, 4),
                                           (BCC, bcc, 2), (ROGER + 0x96, 0x5A5A, 2), (ROGER + 2, 0x8A, 1)]
                    cases.append(dict(name=f'attach kind={kind:#x} 788={d788} face={has_face} bcc={bcc}',
                                      what='attach', entry='001BA8E0', records=(ROGER,), pokes=pokes,
                                      kind=kind, axes=(kind, d788, has_face, bcc),
                                      keep=kind in (0x3B, 0x47) and has_face == 0))
    # 001BA580 over every kind byte, +0x56, the activity byte and D_00810700.
    for kind in range(256):
        for active in (0, 1, -1):
            for value in (0, 1, 2, 3):
                for d700 in (0x0D, 0x11):
                    pokes = face_seed + [(ROGER + 0x56, active & 0xFFFF, 2), (D700, d700, 1)]
                    pokes += [(ACTIVITY + i, value, 1) for i in range(10)]
                    cases.append(dict(name=f'update kind={kind:#x} 56={active} act={value} 700={d700:#x}',
                                      what='update', entry='001BA580', records=(ROGER,), pokes=pokes,
                                      kind=kind, axes=(kind, active, value, d700),
                                      keep=kind in (0x61, 0x47) and active != 0))
    # 001D0690 / 001D06D0 / 001D06E0 called directly on the sentinel face
    # slot (inside 001BA8E0 the +0x81 byte 001D0690 keeps is always
    # rewritten by 001D06D0 next, so only a direct call shows it is kept).
    cases.append(dict(name='reset face+0x70', what='reset', entry='001D0690', records=(ROGER,),
                      pokes=face_seed, target=face + 0x70, axes=(0,), keep=True))
    for a1 in (0, 1, 2, 0x7F, 0x80, 0xFF, 0x100, 0x12345601):
        for which, entry in (('speed', '001D06D0'), ('talk', '001D06E0')):
            cases.append(dict(name=f'{which} a1={a1:#x}', what=which, entry=entry, records=(ROGER,),
                              pokes=face_seed, a1=a1, axes=(a1,), keep=True))
    # 001BA540: +0x56 x +0x90 x the class byte.
    for active in (0, 1, -2):
        for has_face in (0, 1):
            for cls in (0xAA, 0x8A, 0x20, 0x00):
                pokes = face_seed + [(ROGER + 0x56, active & 0xFFFF, 2),
                                     (ROGER + 0x90, face if has_face else 0, 4), (ROGER + 2, cls, 1)]
                cases.append(dict(name=f'release 56={active} face={has_face} cls={cls:#x}',
                                  what='release', entry='001BA540', records=(ROGER,), pokes=pokes,
                                  axes=(active, has_face, cls), keep=True))
    # 001B10B0 directly: a2 == -1 (no +0x40 store) and a few table indices.
    for a1, a2 in ((0x47, -1), (0x47, 0x4A), (0x6B, -1), (0x6B, 0x10)):
        for bcc in (20, 21, 31, 40, 1035):
            pokes = [(BCC, bcc, 2), (ROGER + 0x40, 0xDEADBEEF, 4)]
            cases.append(dict(name=f'bind a1={a1:#x} a2={a2} bcc={bcc}', what='bind', entry='001B10B0',
                              records=(ROGER,), pokes=pokes, a1=a1, a2=a2, axes=(a1, a2, bcc), keep=True))
    # 001C5C90: own lifecycle x the parent's +0x04/+0x09/+0x0D/+0x01 x 001B1020's result.
    # The equipment's bone-0 matrix starts as a sentinel unlike Roger's
    # bone-1 matrix (in every capture the two are equal, so the copy_qw4 of
    # the copying kinds would otherwise be a no-op).
    matrix_seed = equipment_matrix_seed(ee)
    for life in (0, 1, 2, 3, 4, 255):
        for plife in (0, 1, 2, 3):
            for p9 in (0, 21):
                for pkind in PARENT_KINDS:
                    for p1 in (0, 1):
                        for bind in (0, 1):
                            pokes = matrix_seed + [(EQUIP + 4, life, 1), (ROGER + 4, plife, 1),
                                                   (ROGER + 9, p9, 1), (ROGER + 0xD, pkind, 1),
                                                   (ROGER + 1, p1, 1)]
                            cases.append(dict(
                                name=f'equip life={life} plife={plife} p9={p9} pkind={pkind:#x} p1={p1} bind={bind}',
                                what='equipment', entry='001C5C90', records=(ROGER, EQUIP), pokes=pokes,
                                bind=bind, b40=EQUIP + 0x110, axes=(life, plife, p9, pkind, p1, bind),
                                keep=life == 1 and plife == 1 and p9 and p1 == bind))
    return cases


def equipment_matrix_seed(ee):
    """Sentinel finite floats over the equipment's bone-0 world matrix
    (*(EQUIP +0x110) +0x90), asserted different from Roger's bone-1 matrix
    in every word."""
    bone0, bone1 = ee.load(EQUIP + 0x110), ee.load(ROGER + 0x114)
    words = [bits(0.375 * (i + 1) - 2.5 + (0.0625 if i % 5 == 0 else 0.0)) for i in range(16)]
    words[15] = bits(0.9375)
    assert all(w != ee.load(bone1 + 0x90 + 4 * i) for i, w in enumerate(words)), 'sentinel equals bone 1'
    return [(bone0 + 0x90 + 4 * i, w, 4) for i, w in enumerate(words)]


# Every parent kind the equipment unit cases sweep: the ten copying kinds
# (001C5D34..001C5DB0) and two that skip the copy.
PARENT_KINDS = (0x47, 0x4E, 0x54, 0x55, 0x58, 0x59, 0x5A, 0x5D, 0x5E, 0x6A, 0x6B, 0x10)


def float_cases(ee, count, seed):
    """001C5C90 state 1 with Roger's bone-1 world matrix AND the
    equipment's own bone-0 matrix replaced by random bit patterns (finite
    values of every magnitude, zeros, denormals, and a few infinities /
    NaNs), the parent kind cycling through PARENT_KINDS so every copying
    kind is seen copying data that differs from the destination and the
    non-copying kinds run the VU0 path on the equipment's own matrix;
    exercising 00102958, 001026A0, 001028D0 and 00102760."""
    rng = random.Random(seed)
    bone0, bone1 = ee.load(EQUIP + 0x110), ee.load(ROGER + 0x114)
    specials = (0, 0x80000000, 0x00000001, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0x7FC00000,
                0x3F800000, 0xBF800000, 0x00800000)

    def matrix():
        words = []
        for _ in range(16):
            pick = rng.random()
            if pick < 0.08:
                words.append(rng.choice(specials))
            elif pick < 0.7:
                words.append(bits(rng.uniform(-400.0, 400.0)))
            else:
                words.append(rng.getrandbits(32))
        return words
    cases = []
    for n in range(count):
        pkind = PARENT_KINDS[n % len(PARENT_KINDS)]
        pokes = [(bone1 + 0x90 + 4 * i, w, 4) for i, w in enumerate(matrix())]
        pokes += [(bone0 + 0x90 + 4 * i, w, 4) for i, w in enumerate(matrix())]
        pokes += [(EQUIP + 4, 1, 1), (ROGER + 0xD, pkind, 1)]
        cases.append(dict(name=f'float {n} pkind={pkind:#x}', what='equipment', entry='001C5C90 floats',
                          records=(ROGER, EQUIP), pokes=pokes, b40=EQUIP + 0x110))
    return cases


def route_cases(ee, tag):
    """The captured state as it is: the face update Roger runs every frame
    (activity 1 = D_008106D5), the equipment tick, the release, and the
    lifecycle-0 init on Roger's record returned to lifecycle 0; then the
    init and the update again over sentinel slot bytes (sentinel_slot)."""
    bones = ee.load(ee.load(TABLE + 4 * 0x47) + 8, 1)
    return [
        dict(name=f'{tag} update', what='update', entry='route', records=(ROGER,), kind=0x47),
        dict(name=f'{tag} equipment', what='equipment', entry='route', records=(ROGER, EQUIP),
             b40=EQUIP + 0x110, fixed=ee.read(EQUIP + 0xA0, 0x30)),
        dict(name=f'{tag} release', what='release', entry='route', records=(ROGER,)),
        dict(name=f'{tag} init', what='init', entry='route', records=(ROGER,),
             pokes=[(ROGER + 4, 0, 1), (ROGER + 0x90, 0, 4)]),
        dict(name=f'{tag} init seeded', what='init', entry='route', records=(ROGER,),
             pokes=sum((sentinel_slot(a) for a in pop_slots(ee, (0, bones))), [])
             + [(ROGER + 4, 0, 1), (ROGER + 0x90, 0, 4)]),
        dict(name=f'{tag} update seeded', what='update', entry='route', records=(ROGER,), kind=0x47,
             pokes=sentinel_slot(ee.load(ROGER + 0x90))),
    ]


def identity(ee, tag):
    """What the capture holds: the values the lifecycle-0 init and the face
    attach leave behind (checked, not assumed)."""
    face = ee.load(ROGER + 0x90)
    got = dict(callback=ee.load(ROGER + 0x10), kind=ee.load(ROGER + 0xD, 1), status=ee.load(ROGER, 1),
               lifecycle=ee.load(ROGER + 4, 1), descriptor=ee.load(ROGER + 0x30),
               w58=ee.load(ROGER + 0x58) == ee.load(TABLE + 4 * 0x4D),
               model=ee.load(ROGER + 0x44) == ee.load(TABLE + 4 * 0x47),
               draw=ee.load(ROGER + 0x4C), face_active=ee.load(ROGER + 0x56, 2),
               face_bone=ee.load(ROGER + 0x94, 2), shadow_kind=ee.load(ROGER + 0x96, 2),
               pose_bone=ee.load(ROGER + 0x98, 1), cls_bit=ee.load(ROGER + 2, 1) & 0x20,
               face_resource=ee.load(face + 0x60) == ee.load(TABLE + 4 * 0x88),
               speed=ee.load(face + 0x81, 1), equip_parent=ee.load(EQUIP + 0x18),
               equip_callback=ee.load(EQUIP + 0x10), equip_kind=ee.load(EQUIP + 0xD, 1))
    want = dict(callback=CONTROLLER, kind=0x47, status=1, lifecycle=1, descriptor=0x828BD0, w58=True,
                model=True, draw=DRAW, face_active=1, face_bone=7, shadow_kind=0x29, pose_bone=2,
                cls_bit=0x20, face_resource=True, speed=1, equip_parent=ROGER,
                equip_callback=0x1C5C90, equip_kind=0x6B)
    assert got == want, (tag, 'capture identity', got, want)


def main():
    started = time.time()
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'wrong original executable'
    overlay = (DECOMP / 'extract/OVERLAY/AREA11.BIN').read_bytes()
    lib = build_native()
    images = [('playable', REFERENCE / 'playable_ee.bin')]
    routes = sorted(p for p in ROUTE.iterdir() if (p / 'eeMemory.bin').exists())
    extra = [('opening', REFERENCE / 'opening_ee.bin'), ('handoff', REFERENCE / 'handoff_ee.bin'),
             ('roger-encounter', REFERENCE / 'roger-encounter/eeMemory.bin')]
    beats = [(p.name, p / 'eeMemory.bin') for p in routes]
    if not reference_mode.FULL:
        beats = [b for b in beats if b[0][:2] in ('00', '10', '14')]
        extra = []
    base = load_image(images[0][1], elf, overlay)
    ee = RogerEE(elf, base, None)
    callees = check_callee_set(ee)
    resources = (C.c_uint8 * 0x2000000).from_buffer(base)
    identity(ee, 'playable')
    cases = unit_cases(ee)
    axes = [lambda c, i=i: (c['what'], c['axes'][i] if i < len(c['axes']) else None) for i in range(6)]
    selected = reference_mode.select(cases, 2400, 0x8237E0, axes=axes, keep=lambda i, c: c['keep'])
    floats = float_cases(ee, reference_mode.pick(3000, 400), 0x1C5C90)
    counts = {}
    for case in selected + floats + route_cases(ee, 'playable'):
        entry = run_case(ee, lib, resources, case)
        counts[entry] = counts.get(entry, 0) + 1
    outcomes = set(ee.outcomes)
    flows = [ee]
    sites = branch_sites(ee)
    route_count = 1
    for tag, path in beats + extra:
        image = load_image(path, elf, overlay)
        route_ee = RogerEE(elf, image, None)
        identity(route_ee, tag)
        buffer = (C.c_uint8 * 0x2000000).from_buffer(image)
        for case in route_cases(route_ee, tag):
            run_case(route_ee, lib, buffer, case)
            counts['route'] = counts.get('route', 0) + 1
        outcomes |= route_ee.outcomes
        flows.append(route_ee)
        route_count += 1
        del buffer
    skipped = {pc for pc in sites for lo, hi in UNREACHED_BRANCHES.values() if lo <= pc < hi}
    missing = sorted('%06X %s' % (pc, 'taken' if t else 'not taken')
                     for pc in sites - skipped for t in (True, False) if (pc, t) not in outcomes)
    assert not missing, ('branch outcomes never exercised', missing)
    stores = store_sites(ee)
    store_count, unreached_stores, live_keys = check_store_visibility(flows, stores)
    stops = missing_worker_checks(lib, ee, resources)
    OUT.mkdir(parents=True, exist_ok=True)
    report = dict(elf_sha256=ELF_SHA256, overlay_sha256=hashlib.sha256(overlay).hexdigest(),
                  mode=reference_mode.MODE, cases=counts, captures=route_count, call_targets=callees,
                  branch_sites=len(sites), unreached_sites=len(skipped), fault_stops=stops,
                  store_sites=store_count, unreached_store_sites=unreached_stores,
                  store_instances_visible=live_keys, exempt_stores=sorted(hex(a) for a in STORE_EXEMPT),
                  dead_stores=sorted(hex(a) for a in DEAD_STORES),
                  seconds=round(time.time() - started, 1))
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    reference_mode.banner(reference_mode.part(len(selected), len(cases), 'unit cases'),
                          f'{len(floats)} float cases', f'{route_count} captures',
                          f'{callees} call targets (all hooked or translated)')
    print('Roger actor vs original instructions: PASS %s (each run twice: as given, then with every '
          'write-first byte perturbed); every one of %d conditional branches both ways (%d kind-0-only '
          'sites of 001CA5F0 excepted); every live store instance (%d, of %d store sites; %d unreached '
          'kind-0-only, %d dead, %d identical rewrites listed) changes a byte in some run; %d fail-stop '
          'checks (%.1fs)' % (
              ', '.join('%s %d' % kv for kv in sorted(counts.items())), len(sites) - len(skipped),
              len(skipped), live_keys, store_count, unreached_stores, len(DEAD_STORES), len(STORE_EXEMPT),
              stops, time.time() - started))


def missing_worker_checks(lib, ee, resources):
    """A NULL worker faults before any record, slot or global byte changes."""
    names = [f[0] for f in Workers._fields_[1:]]
    checks = 0
    plans = [('init', lambda n, s, r: lib.em_roger_actor_008237E0_init(s, r[ROGER]),
              [(ROGER + 4, 0, 1), (ROGER + 0x90, 0, 4)], ('w_001C63E0', 'w_001CB5B0', 'w_001F0120')),
             ('update', lambda n, s, r: lib.em_roger_actor_001BA580(s, r[ROGER], 0x47), [],
              ('w_001DA6A0', 'w_001D0720')),
             ('equipment', lambda n, s, r: lib.em_roger_actor_001C5C90(s, r[EQUIP], r[ROGER]), [],
              ('w_draw',))]
    for what, call, pokes, needed in plans:
        for name in needed:
            ee.restore()
            prepare(ee, dict(pokes=pokes, b40=EQUIP + 0x110))
            native = Native(lib, ee, dict(b40=EQUIP + 0x110), resources)
            setattr(native.state.workers, name, type(getattr(native.state.workers, name))())
            recs = {a: record_from(ee, a) for a in (ROGER, EQUIP)}
            before = {a: bytes(recs[a]) for a in recs}
            slots, stack = bytes(native.slots), bytes(native.stack)
            activity, spad = bytes(native.activity), bytes(native.spad)
            result = call(native, C.byref(native.state), {a: C.byref(recs[a]) for a in recs})
            assert result == -1 and native.state.fault.code == 1, (what, name, result,
                                                                    native.state.fault.code)
            assert all(bytes(recs[a]) == before[a] for a in recs), (what, name, 'record changed')
            assert bytes(native.slots) == slots and bytes(native.stack) == stack, (what, name)
            assert bytes(native.activity) == activity and bytes(native.spad) == spad, (what, name)
            again = call(native, C.byref(native.state), {a: C.byref(recs[a]) for a in recs})
            assert again == -1, (what, name, 'latched fault must refuse the next call')
            checks += 1
    assert set(n for p in plans for n in p[3]) <= set(names)
    # A draw method other than 001CAA00 with the parent drawn: w_draw stands
    # for 001CAA00 only, so the tick refuses before any write (all workers
    # present).
    for method in (0, DRAW + 4, 0x1CAB00):
        ee.restore()
        pokes = [(EQUIP + 4, 1, 1), (EQUIP + 0x4C, method, 4), (ROGER + 1, 1, 1), (ROGER + 9, 21, 1)]
        prepare(ee, dict(pokes=pokes, b40=EQUIP + 0x110))
        native = Native(lib, ee, dict(b40=EQUIP + 0x110), resources)
        recs = {a: record_from(ee, a) for a in (ROGER, EQUIP)}
        before = {a: bytes(recs[a]) for a in recs}
        slots, spad = bytes(native.slots), bytes(native.spad)
        result = lib.em_roger_actor_001C5C90(C.byref(native.state), C.byref(recs[EQUIP]), C.byref(recs[ROGER]))
        assert result == -1 and native.state.fault.code == 3 and native.state.fault.address == method, (
            'draw method', hex(method), result, native.state.fault.code)
        assert all(bytes(recs[a]) == before[a] for a in recs), ('draw method', 'record changed')
        assert bytes(native.slots) == slots and bytes(native.spad) == spad and not native.calls, 'draw method'
        checks += 1
    return checks


def run_mutants():
    """Every MUTANTS edit, each in its own default-mode run: each must fail
    an assertion (a compile failure or an edit that does not apply is an
    error, not a kill)."""
    import concurrent.futures
    started = time.time()
    OUT.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if k != 'EM_TEST_FULL'}

    def one(index):
        run = subprocess.run([sys.executable, str(Path(__file__).resolve())], cwd=ROOT,
                             env=dict(env, EM_ROGER_MUTANT=str(index)), capture_output=True, text=True)
        lines = run.stderr.strip().splitlines() or ['']
        if run.returncode == 0:
            return 'SURVIVED', ''
        if 'CalledProcessError' in run.stderr or 'does not apply' in run.stderr:
            return 'ERROR', lines[-1]
        return 'killed', lines[-1]
    jobs = int(os.environ.get('EM_TEST_JOBS', '0') or 0) or min(8, max(1, (os.cpu_count() or 2) - 2))
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        results = list(pool.map(one, range(len(MUTANTS))))
    for (name, _, _), (verdict, why) in zip(MUTANTS, results):
        print(f'{verdict:8s} {name}: {why[:150]}')
    bad = [MUTANTS[i][0] for i, r in enumerate(results) if r[0] != 'killed']
    for path in OUT.glob('*mutant_*'):
        path.unlink()
    print('mutants: %d of %d killed (%.1fs)' % (len(MUTANTS) - len(bad), len(MUTANTS), time.time() - started))
    if bad:
        sys.exit(1)


if __name__ == '__main__':
    if sys.argv[1:] == ['--mutants']:
        run_mutants()
    else:
        main()
