#!/usr/bin/env python3
"""Execute the original AREA01 lane-SYS routines and compare em_area01_sys.c.

docs/AREA01_SYS.md. The user's pinned ELF and the captured AREA01 route RAM
(../Extermination/build/s87/route_a01/<beat>/, end-of-beat images) supply
every instruction and every table; none are embedded here.

The oracle is FallEE (tools/test_player_fall_reference.py: every COP1 and
VU0 macro op through tools/ee_float_model.py, the measured model) running
the ORIGINAL routine over a copy of a captured beat. The native module runs
over another copy of the same bytes. Every call that leaves the translated
set is caught on both sides and handled by the same Python policy:
  run   the original callee runs (leaf math, vector / quadword copies, the
        LCG): in the oracle inside the same interpreter, on the native side
        in a second interpreter bound to the native module's own memory;
  stub  the callee is recorded and returns a scripted value; in the 'fx'
        cases it also has a scripted side effect (below).

Lockstep. The original runs first and records, at every call leaving the
set, the callee entry and every RAM / scratchpad line it stored to since
the previous call. The native module is built with its store trace
(EM_AREA01_SYS_STORE_TRACE) and, at the same call, must match: the callee,
the stack pointer, every argument register the callee reads (full 64-bit
images), the float argument registers, the 16 bytes behind each argument
that points at a stack local, the bytes behind stub pointer arguments, and
all of RAM and the scratchpad (only lines either side stored to since the
last check can differ, so exactly those are compared; the first differing
address and the call index are reported). The same memory check runs after
the last store, and then all 32 MiB, the scratchpad and the result.

Stub side effects ('fx' cases). A stub writes nothing, so a translation
that keeps a value across a stubbed call, where the original loads it
again, would pass. Each fx case runs the original once to record what the
translated routines load after each stubbed call, then gives every stub
the side effect of changing exactly those fields (identically on both
sides), so a cached value shows.

Round-4 variants (EM_TEST_FULL=1 only; docs/AREA01_SYS.md section 4):
store-site variants (the bytes a store instruction writes set to their
complement beforehand, and the bytes after a byte / halfword store set to
0xA5), load-site variants (a loaded field's top bit flipped, and the byte
after a loaded byte), stub-result variants (every call to one stubbed
callee returns -1, 2, 0x80000000 or 0x7FFFFFFF), single-load-site fx
variants that flip the top bit, and stub pre-store variants (each stub sets
the fields the original stores after it to the complement of what it
stores, or the bytes after them to 0xA5). Both stacks start filled with
0xA5 and the interpreter's registers with a pattern, so a translation that
leaves a stack local or a register-held value unset differs.

Round 5 (docs/AREA01_SYS.md section 4, "Round 5"): every fx case also has a
deferred-read variant ('fx before': at each stub the fields the original
loaded before it change, each change flipping new bits in both halves), so
a translation that reads a field after a stub where the original read it
before differs; cases may write both private stacks identically ('stack'),
e.g. a record in a run-policy callee's frame; targeted cases make records
alias the routines' own stores (cases_review5); and api_checks exercises
the fail-stop contract (fault codes and addresses, the latch, the refusal
while latched, clear_fault, the first fault kept). QUICK_PINNED and
QUICK_SITE_PINS put into the default run the cheapest case found for each
error only the full run caught (round 7: they are all of the default run).

Round 6 (docs/AREA01_SYS.md section 4, "Round 6"): full mode adds aliasing
variants (a record moved so that a field the routines load lies on the
bytes of one of the four stores executed last before the load: a read
hoisted or cached across that store then differs) and float-compare
variants (exponent-255 and denormal words at every load whose word reaches
a COP1 compare unchanged: a host compare there differs); every register
the native module says it sets must hold the original's value at the call;
cases_review6 holds the round-5 review's inputs; api_checks covers every
entry's NULL context and NULL output, clear_fault(NULL), NULL-bytes and
undersized regions; QUICK_VARIANT_PINS puts six store-site, load-site and
stub-result variants into the default run.

Round 7 (docs/AREA01_SYS.md section 4, "Round 7", the close-out):
cases_review7 holds the last reviews' inputs (an EE c.eq against a zero or
denormal partner, 00158590's a1 with only its upper half set, 001E7D20's
block over the area bytes, an exponent-255 segment start before a
narrowing, 001AA000 keys 0x201 / 0x2FF, a1+0x0B preset); api_checks adds
an access that is a whole region, bytes kept after a fault and the refusal
of any non-NONE fault value. The default run is now only the pinned killing
cases (default_cases, QUICK_PINNED, QUICK_SITE_PINS, QUICK_VARIANT_PINS),
the fault cases and one captured record per routine; every pin also runs in
EM_TEST_FULL=1, which runs everything else.

Register reads. RegScan measures, from the ELF, the state every callee
reads before writing (all paths, jump tables and nested calls included):
the GPRs and FPRs, HI/LO (both pipelines), the COP1 condition flag and
accumulator, and the VU0 state per lane (vf, vi, accumulator, Q, I, R,
flags). The test fails when a callee reads an argument register its
policy does not compare, a caller temporary, any of the hidden state, or
(for executed callees) a caller-saved register. The one data-driven jump,
the documented caller-saved reads of stubbed callees (UNDEFINED paths) and
the two documented VU0 lane reads are listed and enforced exactly.

The test also asserts that every direct call target of the translated
routines has a policy, and in EM_TEST_FULL=1 that both outcomes of every
conditional branch in the translated routines are taken except the listed
unreachable ones (UNREACHABLE, with proof).

Cases (all compared the same way):
  captured  the real owner records of the eight main-route beats, as
            captured;
  random    perturbed state bytes, flags, positions, LCG seeds and scripted
            callee results on those records; world probe segments for
            0019B4C0 over the captured AREA01 collision world;
  t         enumerated paths (every sub-state, both sides of every test,
            LCG seeds solved so the original's own draws hit the grid
            edges, carries and heading bits; the fields the original stores
            poisoned where a case would otherwise start with the stored
            value; the edge values of masks, signed reads and float bounds,
            cases_edges);
  s         synthetic lists, boxes, cells and faces written into zeroed
            RAM, with the leaf tests scripted, so every walker branch of
            001A06A0 / 0019CF50 is taken;
  fault     inputs on which the original reads a value it never set (its
            caller's registers): the native module must refuse with
            EM_AREA01_SYS_FAULT_UNDEFINED.
Three lane functions already translated in other port modules (00191120,
001B0C00, 001FAD70) are checked unchanged through a bridge against the
original. EM_TEST_FULL=1 runs every case; EM_AREA01_SYS_ONLY=<prefix,...>
runs only the cases whose name starts with a prefix (and their fx cases
and variants); EM_AREA01_SYS_GAPS=1 lists the branch outcomes not
taken; EM_AREA01_SYS_SOURCE=<file> tests another copy of the module source
(mutation sweeps); EM_AREA01_SYS_FX=0 skips the fx cases, =only runs only
them. Timings: docs/AREA01_SYS.md section 4.
"""
import bisect
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
import reference_mode as RM  # noqa: E402
import area01_reference_view as AV  # noqa: E402
import test_player_slide_reference as shared  # noqa: E402
from test_player_slide_reference import read_elf, sx32  # noqa: E402
from test_player_fall_reference import FallEE  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route_a01'
OUT = ROOT / 'build' / 'area01' / os.environ.get('EM_LANE', 'sys')
STACK_BASE, STACK_SIZE = 0x7F000000, 0x100000
SP = shared.STACK_TOP
SCRATCH = 0x01E00000            # zero in every captured beat; test records live here
BEATS = ('a01_00_train_room', 'a01_01_tunnel', 'a01_02_shaft_landing', 'a01_03_shaft_locked',
         'a01_04_return_north', 'a01_05_npc_bridge_talk', 'a01_06_return_south', 'a01_07_level_exit')
POOL_HEAD, POOL_STRIDE = 0x275BC0, 0x2F0

# Translated routines (address -> size in bytes, from the census).
FUNCS = {
    0x1287F0: 0x38, 0x128B80: 0x84, 0x128C10: 0xB6C, 0x157CE0: 0x250, 0x158590: 0x278,
    0x158D30: 0x184, 0x159B90: 0x2D4, 0x15A2C0: 0x48C, 0x19B4C0: 0x200, 0x19CF50: 0x3E0,
    0x1A06A0: 0x46C, 0x1A8840: 0x130, 0x1A9E00: 0x160, 0x1AA000: 0x138, 0x1B0300: 0x158,
    0x1B0D80: 0x3C, 0x1B6D70: 0xD0, 0x1B76D0: 0x24, 0x1E3D90: 0x870, 0x1E7C60: 0x44,
    0x1E7CB0: 0x64, 0x1E7D20: 0xE18,
}

# Callee policy: address -> (kind, integer argument registers compared (a0..),
# float argument registers compared (f12..), [(pointer argument, bytes)] for
# stubs). Integer registers are compared as full 64-bit images. The counts
# are checked against a measured scan of what each callee reads (RegScan).
R, S = 'run', 'stub'
CALLEES = {
    0x102948: (R, 2, 0, ()), 0x102958: (R, 2, 0, ()), 0x1026A0: (R, 3, 0, ()),
    0x102738: (R, 2, 0, ()), 0x102760: (R, 2, 0, ()), 0x1028B8: (R, 3, 0, ()),
    0x1028D0: (R, 3, 0, ()), 0x102918: (R, 3, 0, ()), 0x1029C0: (R, 1, 0, ()),
    0x103230: (R, 2, 1, ()), 0x11DE90: (R, 0, 1, ()), 0x11DF78: (R, 0, 1, ()),
    0x11E2A8: (R, 0, 1, ()), 0x11E620: (R, 0, 2, ()), 0x11E748: (R, 0, 1, ()),
    0x122BB8: (R, 0, 0, ()), 0x1B1470: (R, 0, 1, ()), 0x1B12B0: (R, 0, 3, ()),
    0x1B13F0: (R, 2, 1, ()), 0x128600: (R, 1, 0, ()),
    # stubs
    0x1C67E0: (S, 3, 2, ()),                       # 001287F0's clip request
    0x12E070: (S, 1, 0, ()),
    0x1B6F00: (S, 2, 1, ((1, 16),)),
    0x1BA1A0: (S, 2, 0, ()), 0x1BA1F0: (S, 1, 0, ()),
    0x1F4CC0: (S, 2, 0, ((0, 16), (1, 16))),
    0x1B0FD0: (S, 1, 0, ()), 0x1C6380: (S, 1, 0, ()), 0x1B17A0: (S, 1, 0, ()),
    0x1AFC10: (S, 1, 0, ()),
    0x1F4A10: (S, 3, 0, ((0, 64), (1, 16), (2, 16))),
    0x187EC0: (S, 2, 0, ()),
    0x1DD980: (S, 2, 0, ((0, 16), (1, 16))),
    0x1FAE70: (S, 1, 0, ()), 0x1FBC50: (S, 0, 0, ()), 0x1FA790: (S, 2, 0, ()),
    0x1FABB0: (S, 0, 0, ()), 0x1FBD50: (S, 3, 1, ()), 0x1FB9F0: (S, 4, 0, ()),
    0x1B1E20: (S, 2, 0, ()),
    # 00128C10
    0x128AB0: (S, 2, 0, ()), 0x129780: (S, 3, 0, ()), 0x1B2140: (S, 1, 0, ()),
    0x12D580: (S, 3, 0, ()), 0x1C69A0: (S, 1, 0, ()), 0x129FC0: (S, 2, 0, ()),
    0x1289C0: (S, 2, 0, ()), 0x1C63E0: (S, 2, 0, ()), 0x1C25E0: (S, 2, 0, ((1, 16),)),
    0x1C3D60: (S, 2, 0, ()), 0x1B1190: (S, 1, 0, ()), 0x1B1630: (S, 0, 3, ()),
    0x1288D0: (S, 2, 0, ()), 0x1C2770: (S, 3, 0, ()), 0x1C64F0: (S, 1, 1, ()),
    # 00159B90 / 0015A2C0
    0x1C5570: (S, 4, 0, ((1, 16),)), 0x15A200: (S, 3, 0, ()), 0x15A750: (S, 1, 0, ()),
    0x1E9580: (S, 3, 0, ((2, 16),)), 0x1E9E60: (S, 2, 0, ()),
    # 0019CF50 / 001A06A0: the collision leaves run as original code
    0x19ED80: (R, 2, 0, ()), 0x19F1A0: (R, 2, 0, ()), 0x1A4030: (R, 1, 0, ()),
    0x1A50A0: (R, 1, 0, ()), 0x1A5C30: (R, 1, 0, ()),
    # 001E3D90
    0x1CD070: (S, 2, 0, ((0, 16),)), 0x1CD2B0: (S, 0, 4, ()),
    0x1CFAE0: (S, 3, 4, ((2, 16),)), 0x1CFBE0: (S, 5, 0, ((3, 96),)),
    0x1FC3C0: (S, 3, 2, ((1, 4),)), 0x21B9A0: (S, 1, 2, ()),
    # 001E7D20
    0x1CB5F0: (S, 3, 0, ()), 0x1CB6B0: (S, 4, 0, ()), 0x1CB760: (S, 3, 0, ()),
    0x1CB950: (S, 3, 0, ()), 0x1D2E00: (S, 1, 0, ()), 0x1D2DE0: (S, 2, 0, ()),
    0x1E8B90: (S, 1, 1, ((0, 16),)),
}
INDIRECT = (S, 1, 0, ())    # a pointer the original loads and calls with the record


# ======================================================================
# Interpreters
# ======================================================================

REG_FILL, FREG_FILL = 0x01F0A000, 0x449A5000   # mapped RAM (zero in every beat) / a normal float
# Both stacks start filled with this pattern (not zero), so a translation
# that leaves part of an escaping stack local unset hands the callee a
# pattern where the original stored a value.
STACK_FILL = bytes([0xA5]) * 0x100000
LINE = 64                       # memory is compared in 64-byte lines
RAM_LINES = 0x2000000 // LINE   # line ids: RAM lines, then scratchpad lines
STACK_LO, STACK_HI = 0x7F000000, 0x7F100000


def line_of(a):
    """Line id of an original address (RAM or scratchpad), None for the
    private stack (whose frames differ by design: see log_entry)."""
    a &= MASK
    if 0x70000000 <= a < 0x70004000:
        return RAM_LINES + (a - 0x70000000) // LINE
    if a < 0x40000000:
        return (a & 0x1FFFFFF) // LINE
    return None


def line_address(lid):
    return lid * LINE if lid < RAM_LINES else 0x70000000 + (lid - RAM_LINES) * LINE


def line_bytes(mem, spad, lid):
    if lid < RAM_LINES:
        return bytes(mem[lid * LINE:(lid + 1) * LINE])
    o = (lid - RAM_LINES) * LINE
    return bytes(spad[o:o + LINE])


def stack_image(writes):
    """The starting bytes of both private stacks: the fill pattern, with a
    case's `stack` writes ((original address, bytes); records a case places
    in the stack below the entry stack pointer, where the callees' frames
    lie) applied identically to both sides."""
    if not writes:
        return STACK_FILL
    out = bytearray(STACK_FILL)
    for a, data in writes:
        out[a - STACK_LO:a - STACK_LO + len(data)] = data
    return bytes(out)


class SysEE(FallEE):
    """FallEE with branch-outcome recording inside the translated routines
    and a record of every RAM / scratchpad line it stores to (`dirty`,
    since the last take; `pre`, when set, keeps each line's bytes before its
    first store). `stores` records every store instruction of the
    translated routines, for the store-site variants: {pc: {address:
    (size, bytes before its first store there, bytes after its last)}};
    `load_sites` every data load of the translated routines, for the
    load-site variants: {pc: {address: (size, value at its first load)}};
    the stack excluded.

    With `track` set (the ordinary cases of a full run, round 6) it also
    records, for the aliasing and float-compare variants:
      pairs      {(load pc, store pc): (load address, size, store address,
                 size)}: a data load of the translated routines and one of
                 the ALIAS_RECENT distinct store instructions (of the
                 routines or of a run-policy callee) executed last before
                 it, at a different address;
      all_loads  {address: size} of every data load (callees included);
      ptrs       {pointer value: the address it was first loaded from};
      cmp_sites  the load pcs of the translated routines whose loaded word
                 reaches a COP1 compare unchanged (lwc1, or lw then mtc1,
                 with the compared register still holding the loaded bits).
    With `strict` set, a misaligned halfword / word / doubleword access
    stops the run as an address error (the EE raises one there)."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.outcomes = set()
        self.dirty = set()
        self.pre = None
        self.stores = {}
        self.load_sites = {}
        self._pc = None
        self.track = self.strict = False
        self.recent, self.pairs, self.all_loads, self.ptrs = [], {}, {}, {}
        self.fsrc, self.gsrc, self.cmp_sites = {}, {}, set()

    def load(self, address, size=4):
        v = super().load(address, size)
        pc = self._pc
        if pc is not None:
            a = address & MASK
            if self.strict and size in (2, 4, 8) and a % size:
                raise AssertionError(('address', 'misaligned', hex(a), hex(pc)))
            if self.strict and size == 4 and 0x100000 <= a < ELF_END and in_funcs(pc):
                w = struct.unpack_from('<I', ELF_IMAGE, a)[0]
                if w != v and 0x100000 <= w < CODE_END:
                    # a jump-table entry or code pointer of the ELF image
                    # changed: the translation's compiled switch does not
                    # read it
                    raise AssertionError(('address', 'code word changed', hex(a), hex(pc)))
            if not (STACK_LO <= a < STACK_HI) and not (0x100000 <= a < CODE_END):
                funcs = in_funcs(pc)
                if funcs:
                    self.load_sites.setdefault(pc, {}).setdefault(a, (size, v))
                if self.track:
                    if a not in self.all_loads or self.all_loads[a] < size:
                        self.all_loads[a] = size
                    if size == 4 and is_pointer(v) and not v < CODE_END:
                        self.ptrs.setdefault(v, a)
                    if funcs:
                        for spc, sa, ss in self.recent:
                            if (pc, spc) not in self.pairs and not (sa < a + size and a < sa + ss):
                                self.pairs[(pc, spc)] = (a, size, sa, ss)
        return v

    def execute(self, word, pc):
        self._pc = pc
        try:
            super().execute(word, pc)
        finally:
            self._pc = None
        if self.track and in_funcs(pc):
            op = word >> 26
            if op == 49:                                            # lwc1
                ft = word >> 16 & 31
                self.fsrc[ft] = (pc, self.f[ft] & MASK)
            elif op == 35:                                          # lw
                rt = word >> 16 & 31
                if rt:
                    self.gsrc[rt] = (pc, self.r[rt] & MASK)
            elif op == 17:
                rs, rt, fs = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
                if rs == 4:                                         # mtc1
                    g = self.gsrc.get(rt)
                    if g is not None and g[1] == self.r[rt] & MASK:
                        self.fsrc[fs] = g
                elif rs == 16 and word & 63 in (50, 52, 54):        # c.eq / c.lt / c.le
                    for reg in (fs, rt):
                        src = self.fsrc.get(reg)
                        if src is not None and src[1] == self.f[reg] & MASK:
                            self.cmp_sites.add(src[0])

    def branch(self, word, pc):
        b = super().branch(word, pc)
        if b is not None and pc in BRANCH_PCS:
            self.outcomes.add((pc, b[0]))
        return b

    def save(self, address, value, size=4):
        for a in (address, address + size - 1):
            lid = line_of(a)
            if lid is not None:
                if self.pre is not None and lid not in self.pre:
                    self.pre[lid] = line_bytes(self.mem, self.spad, lid)
                self.dirty.add(lid)
        pc = self._pc
        a = address & MASK
        if pc is not None and self.strict and size in (2, 4, 8) and a % size:
            raise AssertionError(('address', 'misaligned', hex(a), hex(pc)))
        if self.track and pc is not None and not STACK_LO <= a < STACK_HI:
            rec = [x for x in self.recent if x[0] != pc]
            rec.append((pc, a, size))
            self.recent = rec[-ALIAS_RECENT:]
        if pc is None or not in_funcs(pc) or STACK_LO <= a < STACK_HI:
            super().save(address, value, size)
            return
        old = self.read(a, size)
        super().save(address, value, size)
        site = self.stores.setdefault(pc, {})
        site[a] = (size, site[a][1] if a in site else old, self.read(a, size))

    def take(self):
        """{line: bytes now} for the lines stored to since the last take."""
        out = {lid: line_bytes(self.mem, self.spad, lid) for lid in self.dirty}
        self.dirty = set()
        return out


def run_nested(ee, fn):
    """Run the original callee `fn` (and all it calls, unhooked) with the
    argument registers as they are; every register except v0 / v1 / f0 is
    restored afterwards."""
    saved = (list(ee.r), list(ee.rh), ee.hi, ee.lo, list(ee.f), ee.acc,
             ee.cond, [list(v) for v in ee.vf], list(ee.vacc), ee.q)
    hooks, ee.hooks = ee.hooks, {}          # the callee and everything it calls run as original
    ee.r[29] = (ee.r[29] - 0x400) & ~15
    ee.r[31] = shared.RETURN
    try:
        ee.run(fn)
    finally:
        ee.hooks = hooks
    v0, v1, f0 = ee.r[2], ee.r[3], ee.f[0]
    (ee.r, ee.rh, ee.hi, ee.lo, ee.f, ee.acc, ee.cond, ee.vf, ee.vacc, ee.q) = saved
    ee.r[2], ee.r[3], ee.f[0] = v0, v1, f0
    return v0, f0


class Script:
    """Scripted stub results: fn -> list of (v0, f0); default (0, 0)."""

    def __init__(self, spec):
        self.queues = {fn: list(values) for fn, values in (spec or {}).items()}

    def take(self, fn):
        q = self.queues.get(fn)
        if q:
            v = q.pop(0)
            return v if isinstance(v, tuple) else (v, 0)
        return (0, 0)


def policy(fn, indirect, forced=()):
    if fn in forced:                      # a run-policy leaf scripted for this case
        return (S,) + CALLEES[fn][1:3] + ((),)
    if fn in CALLEES:
        return CALLEES[fn]
    if fn in indirect:
        return INDIRECT
    raise AssertionError(('call without a policy', hex(fn)))


def log_entry(ee, fn, pol, regs, fregs, sp):
    """What the callee can see at its entry, besides memory: the stack
    pointer, the integer argument registers it reads (full 64-bit images),
    the float argument registers it reads, the 16 bytes behind every
    argument that points into the stack (an escaping local of the caller;
    the rest of the two stacks differs by design: the original keeps saved
    registers there, the translation keeps C locals), and for stubs the
    bytes behind its pointer arguments."""
    kind, na, nf, snaps = pol
    args = tuple(regs[i] & MASK64 for i in range(na))
    floats = tuple(fregs[i] & MASK for i in range(nf))
    stack = tuple(ee.read(a & MASK, 16) for a in args if STACK_LO <= (a & MASK) < STACK_HI - 16)
    snap = tuple(ee.read(regs[i] & MASK, n) for i, n in snaps) if kind == S else ()
    return (hex(fn), hex(sp & MASK), args, floats, stack, snap)


# ======================================================================
# Native side
# ======================================================================

P, U8, U32, I32, U64 = C.POINTER, C.c_uint8, C.c_uint32, C.c_int32, C.c_uint64


class Region(C.Structure):
    _fields_ = [('base', U32), ('size', U32), ('bytes', P(U8))]


class Call(C.Structure):
    _fields_ = [('fn', U32), ('sp', U32), ('a', U64 * 8), ('f', U32 * 4), ('na', U32), ('nf', U32),
                ('v0', U64), ('f0', U32)]


WORKER = C.CFUNCTYPE(C.c_int, C.c_void_p, P(Call))


class Sys(C.Structure):
    _fields_ = [('regions', P(Region)), ('region_count', C.c_uint), ('call', WORKER), ('ctx', C.c_void_p),
                ('sp', U32), ('fault', I32), ('fault_function', U32), ('fault_address', U32), ('view', C.c_void_p)]


NATIVE = None
ELF = None
IMAGES = {}
BRANCH_PCS = set()


def stale(lib, sources):
    """True when `lib` is missing or older than a source, a header in
    src/game, or this test (the private build is reused otherwise)."""
    if not lib.exists():
        return True
    newest = max([(ROOT / s).stat().st_mtime for s in sources] + [Path(__file__).stat().st_mtime] +
                 [h.stat().st_mtime for h in (ROOT / 'src/game').glob('*.h')])
    return newest > lib.stat().st_mtime


TRACE_C = r"""/* Store trace for the native module (test build only): the 64-byte
 * lines of RAM and scratchpad the module stores to since the last reset. */
#include <stdint.h>
#define RAM_LINES (0x2000000u / 64u)
#define LINES (RAM_LINES + 0x4000u / 64u)
static uint8_t dirty[LINES];
uint32_t a01sys_trace_list[LINES];
uint32_t a01sys_trace_count;
void a01sys_trace_store(uint32_t address, unsigned size);
void a01sys_trace_reset(void);
static void mark(uint32_t a)
{
    uint32_t id;
    if (a < 0x2000000u)
        id = a / 64u;
    else if (a - 0x70000000u < 0x4000u)
        id = RAM_LINES + (a - 0x70000000u) / 64u;
    else
        return; /* the private stack */
    if (!dirty[id]) {
        dirty[id] = 1;
        a01sys_trace_list[a01sys_trace_count++] = id;
    }
}
void a01sys_trace_store(uint32_t address, unsigned size)
{
    mark(address);
    mark(address + size - 1u);
}
void a01sys_trace_reset(void)
{
    uint32_t i;
    for (i = 0; i < a01sys_trace_count; i++)
        dirty[a01sys_trace_list[i]] = 0;
    a01sys_trace_count = 0;
}
"""
TRACE = {}


def build_native():
    """The module with its store trace (EM_AREA01_SYS_STORE_TRACE). For a
    mutation sweep, EM_AREA01_SYS_SOURCE names another copy of the source
    (built into its own library)."""
    OUT.mkdir(parents=True, exist_ok=True)
    trace = OUT / 'store_trace.c'
    if not trace.exists() or trace.read_text() != TRACE_C:
        trace.write_text(TRACE_C)
    source = os.environ.get('EM_AREA01_SYS_SOURCE', '')
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    if source:
        source = str(Path(source).resolve())
        lib = OUT / f'area01_sys_{Path(source).stem}.{ext}'
        rebuild = not lib.exists() or Path(source).stat().st_mtime > lib.stat().st_mtime
    else:
        source = 'src/game/em_area01_sys.c'
        lib = OUT / f'area01_sys.{ext}'
        rebuild = stale(lib, [source, str(trace.relative_to(ROOT))])
    if rebuild:
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-ffp-contract=off', '-shared', '-fPIC', '-Isrc', '-Isrc/game',
                        '-DEM_AREA01_SYS_STORE_TRACE=a01sys_trace_store', source, str(trace),
                        '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    TRACE['count'] = U32.in_dll(native, 'a01sys_trace_count')
    TRACE['list'] = (U32 * (RAM_LINES + 0x4000 // LINE)).in_dll(native, 'a01sys_trace_list')
    TRACE['reset'] = native.a01sys_trace_reset
    return native


def native_lines():
    """Line ids the native module stored to since the last call; resets."""
    n = TRACE['count'].value
    if not n:
        return ()
    out = TRACE['list'][:n]
    TRACE['reset']()
    return out


class Lockstep:
    """The oracle's side of the per-call memory check. The original ran
    first; at every call leaving the translated set it recorded the callee
    log entry and the bytes of every line it had stored to since the
    previous call. `pre` holds each line's bytes before the original first
    stored to it; a line the original never stored to still holds its
    starting bytes in the original's final memory."""

    def __init__(self, entries, final, pre, mem, spad):
        self.entries, self.final, self.pre = entries, final, pre
        self.mem, self.spad = mem, spad
        self.shadow = {}

    def pristine(self, lid):
        got = self.pre.get(lid)
        return got if got is not None else line_bytes(self.mem, self.spad, lid)

    def check(self, where, delta, native_dirty, nmem, nspad):
        """Both sides were equal at the previous check, so only lines either
        side stored to since then can differ; compare exactly those."""
        self.shadow.update(delta)
        for lid in sorted(set(native_dirty) | set(delta)):
            want = self.shadow.get(lid)
            if want is None:
                want = self.pristine(lid)
            got = line_bytes(nmem, nspad, lid)
            if got != want:
                k = next(i for i in range(LINE) if got[i] != want[i])
                raise AssertionError(('memory differs', where, 'first address', hex(line_address(lid) + k),
                                      'native', got[k:k + 8].hex(), 'original', want[k:k + 8].hex()))


class NativeRun:
    """One native call over copies of `ram` / `spad` and a private stack."""

    def __init__(self, ram, spad, script, indirect, forced=(), stack=()):
        self.ram = (U8 * len(ram)).from_buffer_copy(ram)
        self.spad = (U8 * len(spad)).from_buffer_copy(spad)
        self.stack = (U8 * STACK_SIZE).from_buffer_copy(stack_image(stack))
        self.regions = (Region * 3)(Region(0, len(ram), C.cast(self.ram, P(U8))),
                                    Region(0x70000000, len(spad), C.cast(self.spad, P(U8))),
                                    Region(STACK_BASE, STACK_SIZE, C.cast(self.stack, P(U8))))
        self.ee = SysEE(ELF, b'', b'')
        self.ee.mem = memoryview(self.ram).cast('B')
        self.ee.spad = memoryview(self.spad).cast('B')
        self.ee.stack = memoryview(self.stack).cast('B')
        self.script, self.indirect, self.log, self.error = script, indirect, [], None
        self.forced = set(forced)
        self.lock = None
        self.fx = {}            # stub index -> [(address, size, value)] (side-effect cases)
        self.stubs = 0
        self.worker = WORKER(self._call)
        self.sys = Sys(self.regions, 3, self.worker, None, SP, 0, 0, 0)

    def dirty(self):
        out = set(native_lines())
        out |= self.ee.dirty
        self.ee.dirty = set()
        return out

    def _call(self, _, pc):
        try:
            c = pc.contents
            pol = policy(c.fn, self.indirect, self.forced)
            kind, na, nf = pol[0], pol[1], pol[2]
            assert c.na >= na and c.nf >= nf and c.na <= 8 and c.nf <= 4, ('native register counts', hex(c.fn),
                                                                           c.na, c.nf)
            k = len(self.log)
            regs = [c.a[i] for i in range(8)]
            entry = log_entry(self.ee, c.fn, pol, regs, list(c.f), c.sp)
            self.log.append(entry)
            if self.lock is not None:
                assert k < len(self.lock.entries), ('native makes more calls than the original', k, entry)
                want, delta, oregs, ofregs = self.lock.entries[k]
                self.lock.check(('call', k, hex(c.fn)), delta, self.dirty(), self.ee.mem, self.ee.spad)
                assert entry == want, ('callee entry differs at call', k, 'native', entry, 'original', want)
                # every register the worker is told is set (na / nf; the
                # header: the ones the original sets for this call, which
                # the policy's compared ones are the first of) holds the
                # original's value at the call
                got = (tuple(c.a[i] & MASK64 for i in range(c.na)), tuple(c.f[i] & MASK for i in range(c.nf)))
                assert got == (oregs[:c.na], ofregs[:c.nf]), ('registers the native says it sets differ at call', k,
                                                             hex(c.fn), 'native', got, 'original',
                                                             (oregs[:c.na], ofregs[:c.nf]))
            if kind == R:
                ee = self.ee
                for i in range(8):
                    ee.r[4 + i] = c.a[i] & MASK64
                for i in range(4):
                    ee.f[12 + i] = c.f[i] & MASK
                ee.r[29] = c.sp
                v0, f0 = run_nested(ee, c.fn)
            else:
                v0, f0 = self.script.take(c.fn)
                for address, size, value in self.fx.get(self.stubs, ()):
                    self.ee.save(address, value, size)      # traced like a run callee's store
                self.stubs += 1
            c.v0 = v0 & MASK64
            c.f0 = f0 & MASK
            return 0
        except Exception as e:  # noqa: BLE001 - reported after the call returns
            if self.error is None:
                self.error = e
            return -1


# ======================================================================
# Oracle side
# ======================================================================

def oracle_run(case, ram, spad, cls=None, on_stub=None, track=False):
    """The original over `ram` / `spad` (bytearrays, used in place).
    Returns (ee, log, v0, entries, final delta): entries[k] = (log entry,
    {line: bytes} stored since the previous call, a0..t3 as 64-bit images,
    f12..f15 bits) at the k-th call.
    on_stub(ee, k), when given, runs after the k-th stubbed call has set its
    result: the stub's scripted side effects (side-effect cases)."""
    ee = (cls or SysEE)(ELF, b'', b'')
    ee.mem, ee.spad = ram, spad
    ee.track = track
    ee.strict = bool(case.get('alias'))
    if is_variant(case):
        ee.limit = VARIANT_STEP_LIMIT
    ee.stack[:] = stack_image(case.get('stack', ()))
    ee.pre = {}
    script, log, entries = Script(case['script']), [], []
    indirect = set(case.get('indirect', ()))
    forced = set(case.get('forced', ()))
    targets = set(CALLEES) | indirect
    stubs = [0]

    def make(fn):
        pol = policy(fn, indirect, forced)

        def hook(e):
            regs = [e.r[4 + i] for i in range(8)]
            entry = log_entry(e, fn, pol, regs, [e.f[12 + i] for i in range(4)], e.r[29])
            log.append(entry)
            entries.append((entry, e.take(), tuple(r & MASK64 for r in regs),
                            tuple(e.f[12 + i] & MASK for i in range(4))))
            if pol[0] == R:
                run_nested(e, fn)
            else:
                v0, f0 = script.take(fn)
                e.r[2] = sx32(v0) & MASK64
                e.f[0] = f0 & MASK
                if on_stub is not None:
                    on_stub(e, stubs[0])
                stubs[0] += 1
        return hook
    ee.hooks = {fn: make(fn) for fn in targets}
    # every register the caller does not set holds a pattern, not zero: an
    # original that reads a register it never set (the UNDEFINED paths) then
    # sees a value no translation constant stands in for
    for i in range(1, 32):
        if i not in (28, 29, 31):
            ee.r[i] = REG_FILL + i * 0x40
    for i in range(32):
        ee.f[i] = FREG_FILL + i
    for i, v in enumerate(case['args']):
        ee.r[4 + i] = sx32(v) & MASK64
    for i, v in enumerate(case.get('fargs', ())):
        ee.f[12 + i] = v & MASK
    ee.r[29] = SP
    ee.r[31] = shared.RETURN
    ee.run(case['fn'])
    return ee, log, ee.r[2] & MASK, entries, ee.take()


# ======================================================================
# Stub side effects (the 'fx' cases)
# ======================================================================
#
# A stub writes nothing, so a translation that keeps a value in a local
# across a stubbed call where the original loads it again after the call
# would pass every ordinary case. An fx case gives every stub a scripted
# side effect, identical on both sides: after the k-th stubbed call it
# changes every field the original loads (in the translated routines) after
# that call and before the next one. Pass 1 runs the original once and
# records those loads; pass 2 runs the original with the changes applied at
# each stub and records the exact bytes written, and the native run applies
# the same bytes at the same stub. The lockstep has already shown both
# memories equal at the stub's entry, so the change is identical; the
# writes are traced on both sides like any store.
#
# The change is small so the path mostly stays the same: byte and halfword
# fields flip bit 0, words and doublewords flip bit 4. Words holding a code
# address are left alone (a jump-table entry, a handler), except the
# record's handler pointer (the case's `indirect` pointer): it moves to a
# second address that is stubbed on both sides, so a translation calling a
# handler it loaded before the stub calls the wrong one.

CODE_END = 0x230000             # the ELF's code ends here (data from 0x230000 on)
ELF_END = 0x275B00              # the ELF's loaded file image ends here
ELF_IMAGE = b''                 # its bytes (main() sets it), for the aliasing variants
VARIANT_STEP_LIMIT = 5_000_000  # instructions per run in a variant (ordinary runs: 50 million)
FUNC_RANGES = ()


def in_funcs(pc):
    for lo, hi in FUNC_RANGES:
        if lo <= pc < hi:
            return True
    return False


class RecEE(SysEE):
    """Pass 1: the data loads the translated routines make, keyed by the
    number of stubbed calls made before them."""

    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.window = 0
        self.loads = {}
        self.stores_w = {}

    def save(self, address, value, size=4):
        pc = self._pc
        super().save(address, value, size)
        a = address & MASK
        if pc is not None and in_funcs(pc) and not (STACK_LO <= a < STACK_HI):
            self.stores_w.setdefault(self.window, {}).setdefault((a, size), self.read(a, size))

    def load(self, address, size=4):
        v = super().load(address, size)
        pc = self._pc
        if pc is not None and in_funcs(pc):
            a = address & MASK
            if not (STACK_LO <= a < STACK_HI) and not (0x100000 <= a < CODE_END):
                self.loads.setdefault(self.window, {}).setdefault((a, size), (v, pc))
        return v


def fx_plan(case, before=False):
    """Pass 1: {stub index k: [(address, size, value at the load, load pc)]}
    for the loads made after the k-th stub and before the next, and the
    alternate handler pointers the plan uses. With before=True (the
    deferred-read variants) the plan instead lists, at the k-th stub, the
    loads made before it (after the previous stub): a translation that
    reads such a field after the stub, where the original read it before,
    then sees the changed value."""
    ram, spad = prepared(case)

    def count(e, k):
        e.window = k + 1
    ee, *_ = oracle_run(case, ram, spad, cls=RecEE, on_stub=count)
    indirect = set(case.get('indirect', ()))
    plan, alts = {}, set()
    for w, loads in ee.loads.items():
        if w == 0 and not before:
            continue
        items = []
        for (a, size), (v, pc) in sorted(loads.items()):
            if size == 4 and 0x100000 <= v < CODE_END:
                if v not in indirect:
                    continue
                alts.add(v ^ 0x10)
            items.append((a, size, v, pc))
        if items:
            plan[w if before else w - 1] = items
    return plan, sorted(alts)


def fx_store_plan(case, after):
    """Pass 1 for the stub pre-store variants: {stub index k: [(address,
    size, value)]} setting, at the k-th stub, every field the translated
    routines store after that stub and before the next to the complement
    of the bytes they store there first (after=False), or the bytes just
    after a byte or halfword field to 0xA5 (after=True)."""
    ram, spad = prepared(case)

    def count(e, k):
        e.window = k + 1
    ee, *_ = oracle_run(case, ram, spad, cls=RecEE, on_stub=count)
    plan = {}
    for w, stores in ee.stores_w.items():
        if w == 0:
            continue
        items = []
        for (a, size), data in sorted(stores.items()):
            if after:
                if size < 4 and not protected(case, a + size, size):
                    items.append((a + size, size, int.from_bytes(bytes([0xA5]) * size, 'little')))
            elif not protected(case, a, size):
                items.append((a, size, int.from_bytes(bytes(b ^ 0xFF for b in data), 'little')))
        if items:
            plan[w - 1] = items
    return plan


class PreStore:
    """Pass 2 of a pre-store variant: the planned values written at each
    stub (identically on both sides, like Perturb)."""

    def __init__(self, plan):
        self.plan, self.log = plan, {}

    def __call__(self, e, k):
        out = []
        for a, size, v in self.plan.get(k, ()):
            e.save(a, v, size)
            out.append((a, size, v))
        if out:
            self.log[k] = out


# Fields whose value selects one of the documented UNDEFINED inputs
# (section 2 of docs/AREA01_SYS.md): the side effect keeps them inside the
# defined range (flip bit 0, or leave the field alone), so an fx case never
# turns a defined input into one the native module must refuse.
FX_RANGE = {
    0x275C10: (0, 14, "001E7D20's index into its 15-entry local table (outside 0..14 the original reads "
                      "past it)"),
}


class Perturb:
    """Pass 2: the stub side effects, applied in the oracle; `log` keeps
    the bytes written at each stub for the native run."""

    def __init__(self, plan, indirect, site=None, high=False, before=False):
        self.plan, self.indirect, self.log, self.site = plan, set(indirect), {}, site
        self.high, self.before = high, before
        self.flips = {}

    def __call__(self, e, k):
        out = []
        for a, size, v, pc in self.plan.get(k, ()):
            if self.site is not None and pc != self.site:
                continue
            cur = e.load(a, size)
            if size == 4 and v in self.indirect:
                if cur != v:
                    continue
                new = v ^ 0x10
            elif size == 4 and 0x100000 <= cur < CODE_END:
                continue
            elif a in FX_RANGE:
                if self.high:
                    continue
                new = cur ^ 1
                if not FX_RANGE[a][0] <= new <= FX_RANGE[a][1]:
                    continue
            elif self.high:
                # the top bit of the field (a signed and an unsigned reading
                # differ, and so do a narrower and the full one); not for a
                # word that holds a pointer
                if size == 4 and is_pointer(cur):
                    continue
                new = cur ^ (1 << (8 * size - 1))
            elif self.before:
                # deferred-read variant: the n-th change of a field flips
                # bits no earlier change of it flipped, so every value the
                # field holds later differs from the value the original
                # read; both halves of a field change (a consumer of only
                # its high or low part sees it): a byte flips bit n, a
                # halfword bits n and 8 + n, a word bits 4 + n and 16 + n (a
                # pointer word only bit 4 + n, so it stays near its target),
                # a doubleword bits 4 + n and 36 + n
                n = self.flips.get((a, size), 0)
                self.flips[(a, size)] = n + 1
                if size == 1:
                    mask = 1 << n % 8
                elif size == 2:
                    mask = (1 << n % 8) | (1 << (8 + n % 8))
                elif size == 4 and is_pointer(cur):
                    mask = 1 << (4 + n % 20)
                elif size == 4:
                    mask = (1 << (4 + n % 12)) | (1 << (16 + n % 12))
                else:
                    mask = (1 << (4 + n % 28)) | (1 << (36 + n % 28))
                new = cur ^ mask
            else:
                new = cur ^ (1 if size < 4 else 0x10)
            e.save(a, new, size)
            out.append((a, size, new))
        if out:
            self.log[k] = out


# ======================================================================
# Native entry points per routine
# ======================================================================

def native_call(fn, sysp, args, fargs):
    out = I32(0)
    N = NATIVE
    a = list(args) + [0] * 8
    if fn == 0x1287F0:
        return N.em_area01_sys_001287F0(sysp, U32(a[0]), U32(a[1]), I32(s32(a[2])), U32(fargs[0])), None
    single = {0x158D30: 'em_area01_sys_00158D30', 0x159B90: 'em_area01_sys_00159B90',
              0x15A2C0: 'em_area01_sys_0015A2C0', 0x128C10: 'em_area01_sys_00128C10',
              0x1E3D90: 'em_area01_sys_001E3D90', 0x1E7D20: 'em_area01_sys_001E7D20'}
    if fn in single:
        return getattr(N, single[fn])(sysp, U32(a[0])), None
    if fn == 0x158590:
        return N.em_area01_sys_00158590(sysp, U32(a[0]), I32(s32(a[1])), I32(s32(a[2]))), None
    if fn in (0x1A8840, 0x1A9E00):
        return getattr(N, 'em_area01_sys_%08X' % fn)(sysp, U32(a[0]), U32(a[1])), None
    if fn == 0x1AA000:
        return N.em_area01_sys_001AA000(sysp, U32(a[0]), U32(a[1]), U32(a[2]), U32(a[3])), None
    if fn == 0x1B0300:
        return N.em_area01_sys_001B0300(sysp), None
    if fn == 0x1E7C60:
        return N.em_area01_sys_001E7C60(sysp, U32(a[0]), U32(fargs[0])), None
    rc = None
    if fn == 0x128B80:
        rc = N.em_area01_sys_00128B80(sysp, U32(a[0]), U32(a[1]), C.byref(out))
    elif fn == 0x157CE0:
        rc = N.em_area01_sys_00157CE0(sysp, U32(a[0]), I32(s32(a[1])), C.byref(out))
    elif fn == 0x1B0D80:
        rc = N.em_area01_sys_001B0D80(sysp, U32(a[0]), C.byref(out))
    elif fn == 0x1B6D70:
        rc = N.em_area01_sys_001B6D70(sysp, U32(a[0]), U32(a[1]), U32(a[2]), C.byref(out))
    elif fn == 0x1B76D0:
        rc = N.em_area01_sys_001B76D0(sysp, U32(a[0]), U32(a[1]), U32(a[2]), C.byref(out))
    elif fn == 0x1E7CB0:
        rc = N.em_area01_sys_001E7CB0(sysp, C.byref(out))
    elif fn in (0x19CF50, 0x1A06A0):
        rc = getattr(N, 'em_area01_sys_%08X' % fn)(sysp, C.byref(out))
    elif fn == 0x19B4C0:
        rc = N.em_area01_sys_0019B4C0(sysp, U32(a[0]), U32(a[1]), U32(a[2]), I32(s32(a[3])), C.byref(out))
    else:
        raise AssertionError(('no native entry', hex(fn)))
    return rc, out.value & MASK


RETURNS = {0x128B80, 0x157CE0, 0x1B0D80, 0x1B6D70, 0x1B76D0, 0x1E7CB0, 0x19CF50, 0x1A06A0, 0x19B4C0}


def s32(v):
    v &= MASK
    return v - (1 << 32) if v & 0x80000000 else v


LIBC = C.CDLL(None)
LIBC.memcmp.restype = C.c_int
LIBC.memcmp.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t]


def same(native_buf, oracle_buf):
    """Byte equality of a ctypes array and a bytearray without copying."""
    n = len(oracle_buf)
    if C.sizeof(native_buf) != n:
        return False
    view = (C.c_char * n).from_buffer(oracle_buf)
    try:
        return LIBC.memcmp(C.addressof(native_buf), C.addressof(view), n) == 0
    finally:
        del view


def first_differences(a, b, base=0, limit=6):
    out, i, n = [], 0, len(a)
    while i < n and len(out) < limit:
        j = i + 0x10000
        if a[i:j] != b[i:j]:
            for k in range(i, min(j, n)):
                if a[k] != b[k]:
                    out.append((hex(base + k), a[k], b[k]))
                    if len(out) >= limit:
                        break
        i = j
    return out


# ======================================================================
# One case
# ======================================================================

def image(beat):
    if beat not in IMAGES:
        IMAGES[beat] = ((ROUTE / beat / 'eeMemory.bin').read_bytes(), (ROUTE / beat / 'scratchpad.bin').read_bytes())
    return IMAGES[beat]


def prepared(case):
    ram, spad = image(case['beat'])
    ram, spad = bytearray(ram), bytearray(spad)
    for address, data in case.get('writes', ()):
        if 0x70000000 <= address < 0x70004000:
            spad[address - 0x70000000:address - 0x70000000 + len(data)] = data
        else:
            ram[address:address + len(data)] = data
    return ram, spad


def is_variant(case):
    """A case built by changing another case's input to expose one kind of
    translation error (the not-comparable and UNDEFINED rules apply)."""
    return bool(case.get('store_site') or case.get('load_site') or case.get('stub_result') or case.get('fx_high')
                or case.get('fx_store') or case.get('fx_before') or case.get('alias'))


# The routine that reports each fault case's UNDEFINED input (by the case's entry).
UNDEFINED_AT = {0x1E3D90: 0x1E3D90, 0x1E7D20: 0x1E7D20, 0x19CF50: 0x19CF50, 0x19B4C0: 0x19CF50}


def undefined_path(where, outcomes):
    """True when the original's branch outcomes show the documented
    UNDEFINED path of the routine `where` (docs/AREA01_SYS.md section 2):
    0019CF50 when its span test (0x19D128) never finds a new best (every
    execution of it went the 'not better' way); 001E3D90 when its variant
    switch took the 'above 2' way (0x1E3FAC not taken, UNREACHABLE for a
    defined input)."""
    if where == 0x19CF50:
        return (0x19D128, True) in outcomes and (0x19D128, False) not in outcomes
    if where == 0x1E3D90:
        return (0x1E3FAC, False) in outcomes
    return False


def run_case(case):
    """Returns (branch outcomes, steps, calls, scripted writes, fx load
    sites, {'stores': SysEE.stores, 'loads': SysEE.load_sites, 'stubs':
    the stubbed callees the original called}). Raises on any difference.
    The original runs first; the native run then compares, at every call
    leaving the module, the callee entry (stack pointer, argument registers,
    stack locals and pointer bytes it reads) and all of RAM and the
    scratchpad against the original at the same call, and again after its
    last store; finally all 32 MiB, the scratchpad and the result."""
    fx, sites = None, {}
    if case.get('fx_store'):
        fx = PreStore(fx_store_plan(case, case['fx_store'] == 'after'))
    elif case.get('fx'):
        plan, alts = fx_plan(case, case.get('fx_before', False))
        if not case.get('fx_before'):
            for items in plan.values():
                for a, size, v, pc in items:
                    sites.setdefault(pc, v)
        case = dict(case, indirect=list(case.get('indirect', ())) + alts)
        fx = Perturb(plan, case['indirect'], case.get('fx_site'), case.get('fx_high', False),
                     case.get('fx_before', False))
    ram, spad = prepared(case)
    nat = NativeRun(ram, spad, Script(case['script']), set(case.get('indirect', ())), case.get('forced', ()),
                    case.get('stack', ()))
    if AV.ENABLED:
        nat.canonical_view = AV.CanonicalView(nat.regions)
        nat.canonical_view.install(nat.sys)
    native_lines()                           # a previous case's leftovers
    where = (case['name'],)
    if case.get('fault'):
        # the original reads a value it never set on this path: the native must
        # refuse, in the routine that reads it, after making exactly the calls
        # the original makes before that point (compared call by call, memory
        # included, against the original run with the interpreter's register
        # values standing in for the unset ones), and the original must take
        # the documented UNDEFINED path
        ee, entries = None, None
        try:
            ee, _, _, entries, last = oracle_run(case, bytearray(ram), bytearray(spad))
        except AssertionError:
            pass
        if ee is not None:
            nat.lock = Lockstep(entries, last, ee.pre, ee.mem, ee.spad)
        rc, _ = native_call(case['fn'], C.byref(nat.sys), case['args'], case.get('fargs', ()))
        assert nat.error is None, (where, 'native worker error before the refusal', repr(nat.error))
        assert rc == -1 and nat.sys.fault == 4, (where, 'expected the UNDEFINED fault', rc, nat.sys.fault)
        assert nat.sys.fault_address == UNDEFINED_AT[case['fn']], (where, 'UNDEFINED reported by',
                                                                   hex(nat.sys.fault_address))
        if ee is not None:
            assert len(nat.log) <= len(entries), (where, 'native makes more calls than the original')
            want = nat.sys.fault_address
            assert want == 0x1E7D20 or undefined_path(want, ee.outcomes), (where, 'the original did not take '
                                                                           'the UNDEFINED path')
        return set(), 0, len(nat.log), 0, {}, {}
    track = (RM.FULL or case['name'] in TRACK_BASES) and fx is None and not is_variant(case)
    try:
        ee, olog, ov0, entries, last = oracle_run(case, ram, spad, on_stub=fx, track=track)
    except AssertionError as e:
        if is_variant(case) and e.args \
                and isinstance(e.args[0], tuple) \
                and e.args[0][:1] == ('address',):
            # the variant sent the original outside the memory the test maps
            # (e.g. a flipped index into a kseg0 mirror): not comparable here
            return set(), -1, 0, 0, {}, {}
        if is_variant(case) and e.args and isinstance(e.args[0], tuple) and e.args[0][:1] == ('step limit',) \
                and not in_funcs(int(e.args[0][1], 16)):
            # the variant's value keeps a run-policy callee's loop from
            # ending (001B1470's angle wrap given an exponent-255 or huge
            # angle, which subtracting 2 pi leaves unchanged): the original
            # does not return: not comparable
            return set(), -1, 0, 0, {}, {}
        if case.get('alias') and e.args and isinstance(e.args[0], tuple) and e.args[0][:1] == ('opcode',) \
                and not 0x100000 <= int(e.args[0][-1], 16) < CODE_END:
            # a moved record put a data word where the original loads a code
            # address and jumps: it executes data (the EE raises an
            # exception there), not the routine: not comparable
            return set(), -1, 0, 0, {}, {}
        raise AssertionError((where, 'the original run stopped', e.args)) from e
    if fx is not None:
        nat.fx = fx.log
    nat.lock = Lockstep(entries, last, ee.pre, ee.mem, ee.spad)
    rc, nv0 = native_call(case['fn'], C.byref(nat.sys), case['args'], case.get('fargs', ()))
    if nat.error is not None:
        raise AssertionError((where, 'native worker error', repr(nat.error)))
    variant = is_variant(case)
    if rc == -1 and nat.sys.fault == 4 and variant and undefined_path(nat.sys.fault_address, ee.outcomes):
        # a variant that made the input one of the documented UNDEFINED ones:
        # the original took that path too, and the native refused it
        return set(), 0, len(nat.log), 0, {}, {}
    if rc == -1 and nat.sys.fault == 3 and variant and 0x2000000 <= nat.sys.fault_address < 0x40000000:
        # a variant that sent the original to an address the interpreter
        # folds onto RAM (a mirror) and the native module does not map:
        # not comparable here (counted with the unmapped ones)
        return set(), -1, 0, 0, {}, {}
    assert rc == 0, (where, 'native faulted', rc, nat.sys.fault, hex(nat.sys.fault_function),
                     hex(nat.sys.fault_address))
    assert len(nat.log) == len(entries), (where, 'native makes fewer calls than the original', len(nat.log),
                                          len(entries), entries[len(nat.log)][0] if len(nat.log) < len(entries) else ())
    try:
        nat.lock.check(('after the last store',), last, nat.dirty(), nat.ee.mem, nat.ee.spad)
    except AssertionError as e:
        raise AssertionError((where,) + e.args) from e
    if case['fn'] in RETURNS:
        assert nv0 == ov0, (where, 'result', hex(nv0), hex(ov0))
    if olog != nat.log:
        for i, (a, b) in enumerate(zip(olog, nat.log)):
            if a != b:
                raise AssertionError((where, 'call log differs at', i, 'original', a, 'native', b))
        raise AssertionError((where, 'call log length', len(olog), len(nat.log), olog[len(nat.log):][:2],
                              nat.log[len(olog):][:2]))
    if not same(nat.ram, ee.mem):
        raise AssertionError((where, 'RAM differs (address, native, original)',
                              first_differences(bytes(nat.ram), bytes(ee.mem))))
    if not same(nat.spad, ee.spad):
        raise AssertionError((where, 'scratchpad differs',
                              first_differences(bytes(nat.spad), bytes(ee.spad), 0x70000000)))
    return (ee.outcomes, ee.steps, len(olog), sum(len(v) for v in fx.log.values()) if fx else 0, sites,
            dict(stores=ee.stores, loads=ee.load_sites, path=hash(frozenset(ee.outcomes)),
                 track=dict(pairs=ee.pairs, all_loads=ee.all_loads, ptrs=ee.ptrs, cmp=ee.cmp_sites) if track else None,
                 stubs=sorted({int(e[0], 16) for e in olog
                                                                      if policy(int(e[0], 16), set(case.get('indirect', ())), case.get('forced', ()))[0] == S})))


# ======================================================================
# Case generation
# ======================================================================

def u32(ram, a):
    return struct.unpack_from('<I', ram, a & 0x1FFFFFF)[0]


def pool(ram):
    a, seen, out = u32(ram, POOL_HEAD), set(), []
    while a and a not in seen and len(out) < 0x100:
        seen.add(a)
        out.append((a, u32(ram, a + 0x10)))
        a = u32(ram, a + 0x1C)
    return out


def owners(beat, fn):
    ram, _ = image(beat)
    return [a for a, cb in pool(ram) if cb == fn]


def W8(a, v): return (a, struct.pack('<B', v & 0xFF))
def W16(a, v): return (a, struct.pack('<H', v & 0xFFFF))
def W32(a, v): return (a, struct.pack('<I', v & MASK))
def WF(a, v): return (a, struct.pack('<f', v))
def FB(v): return struct.unpack('<I', struct.pack('<f', v))[0]


def case(name, fn, beat, args, script=None, writes=(), fargs=(), indirect=(), fault=False, forced=()):
    return dict(name=name, fn=fn, beat=beat, args=list(args), script=script or {}, writes=list(writes),
                fargs=list(fargs), indirect=list(indirect), fault=fault, forced=list(forced))


LCG_A, LCG_C = 1103515245, 12345


def seed_writes(ram, rng, wants):
    """Writes that make the original LCG (00122BB8: the seed word at
    *(D_0024295C) + 0x58) produce draws satisfying wants[i](draw i) for the
    next len(wants) calls (None = any), found by search."""
    for _ in range(200000):
        seed = rng.getrandbits(32)
        state, ok = seed, True
        for want in wants:
            state = (state * LCG_A + LCG_C) & MASK
            if want is not None and not want(state & 0x7FFFFFFF):
                ok = False
                break
        if ok:
            return [W32(u32(ram, 0x24295C) + 0x58, seed)]
    raise AssertionError('no seed found')


def cell_is(cell):
    return lambda v: ((v >> 16) * 32) >> 15 == cell


def cases_small(rng):
    out = []
    B0 = BEATS[0]
    actors = owners(B0, 0x128C10) + owners(B0, 0x15A2C0)
    # 001B0D80
    for i, v in enumerate((-200.0, -200.00002, -199.99998, -1e30, 0.0, 12.5, -0.0)):
        a = actors[i % len(actors)]
        out.append(case(f'1B0D80 y={v}', 0x1B0D80, B0, [a], writes=[WF(a + 0xB4, v)]))
    for beat in BEATS:
        for a in owners(beat, 0x128C10)[:2]:
            out.append(case(f'1B0D80 {beat} {a:x}', 0x1B0D80, beat, [a]))
    # 001287F0
    for a in actors[:3]:
        b = a + 0x1F0
        ram, _ = image(B0)
        cur = struct.unpack_from('<h', ram, b + 0xF8)[0]
        for clip in (cur, cur + 1, 6, -3, 0x10006):
            out.append(case(f'1287F0 {a:x} clip {clip}', 0x1287F0, B0, [a, b, clip & MASK], fargs=[FB(4.0)]))
    # 00128B80
    for a in actors[:2]:
        for s36 in (0, 5):
            for g in (0, 1):
                out.append(case(f'128B80 {a:x} {s36} {g}', 0x128B80, B0, [a, a + 0x1F0],
                                writes=[W16(a + 0x36, s36), W16(a + 0x34, 0x1234), W8(0x81080F, g),
                                        # poisoned so each clear is observable
                                        W8(a + 5, 0xA5), W8(a + 6, 0x5A), W8(a + 7, 0xC3)]))
    # 00157CE0
    a = actors[0]
    for bflags in (0, 4, 5):
        for typ in (0x38, 0x12):
            for c7f in (0, 1):
                for s34, cb2 in ((3, 5), (3, 6), (-2, -5)):
                    for y in (0.0, 5.99, 6.0, 7.0):
                        for c63, cb4 in ((2, 60), (2, 61), (0x80, -3840 & 0xFFFF)):
                            if typ == 0x38 and (c63, cb4) != (2, 60):
                                continue
                            if typ != 0x38 and (c7f or (s34, cb2) != (3, 5) or y):
                                continue
                            out.append(case(f'157CE0 b{bflags} t{typ:x} {c7f} {s34}/{cb2} y{y} {c63}/{cb4}',
                                            0x157CE0, B0, [a, 2],
                                            writes=[W8(a + 0xB, bflags), W8(a + 3, typ), W8(0x810C7F, c7f),
                                                    W16(a + 0x34, s34), W16(0x810CB2, cb2), WF(a + 0xB4, y),
                                                    WF(0x810354, 0.0), W8(0x810C63, c63), W16(0x810CB4, cb4)]))
    # 00158590
    for p in owners(B0, 0x159B90) + owners(B0, 0x158D30) + actors[:1]:
        for mode in (-2, -1, 0, 1, 2, 7):
            for a1 in (0, 1):
                for flip in (False, True):
                    writes = [WF(p + 0xF0, -1000.0 if flip else 1000.0), WF(p + 0xF4, 0.0), WF(p + 0xF8, 0.0)]
                    if a1:
                        writes.append(WF(p + 0xBC, 3.5))       # the w the quadword copy brings
                    out.append(case(f'158590 {p:x} m{mode} a{a1} f{flip}', 0x158590, B0, [p, a1, mode & MASK],
                                    writes=writes))
    # 00158D30
    p = owners(B0, 0x158D30)[0]
    ram, _ = image(B0)
    ind = u32(ram, p + 0x4C)
    for st in (0, 1, 2, 3, 4):
        for s2e in (0, 1, 2):
            for bb in (0, 1):
                for draw in (0, 1):
                    if st != 1 and (s2e or bb or draw):
                        continue
                    out.append(case(f'158D30 s{st} {s2e} {bb} {draw}', 0x158D30, B0, [p],
                                    writes=[W8(p + 4, st), W16(p + 0x2E, s2e), W8(p + 0xB, bb)],
                                    script={0x1B17A0: [draw]}, indirect=[ind]))
    # 001A8840 / 001A9E00 / 001AA000 over pairs of records
    player = 0x8102B0
    for i in range(24):
        a0 = rng.choice([player] + actors)
        a1 = rng.choice(actors)
        if a1 == a0:
            continue
        ram, _ = image(B0)
        px, py, pz = struct.unpack_from('<3f', ram, a1 + 0xB0)
        ext = u32(ram, a1 + 0x30)
        e0, e1, e2 = struct.unpack_from('<3f', ram, ext)
        near = rng.random() < 0.7
        dx = rng.uniform(-1.2, 1.2) * (e0 if near else 50)
        dy = rng.uniform(-1.2, 1.2) * ((e1 + 1.5) if near else 50)
        dz = rng.uniform(-1.2, 1.2) * (e2 if near else 50)
        writes = [WF(a0 + 0xA0, px + dx), WF(a0 + 0xA4, py + dy), WF(a0 + 0xA8, pz + dz),
                  W8(a1 + 0xD, rng.choice((0, 0, 1, 2))), W8(a1 + 0xB, rng.choice((0, 1))),
                  W8(0x810707, rng.choice((0, 1))), W8(a0, rng.choice((0, 1, 4, 5))), W8(a1 + 3, rng.choice((0, 0, 1)))]
        out.append(case(f'1A8840 #{i}', 0x1A8840, B0, [a0, a1], writes=writes))
        out.append(case(f'1A9E00 #{i}', 0x1A9E00, B0, [a0, a1], writes=writes))
        k = rng.choice((0x100, 0x200, 0x300))
        w2 = writes + [WF(a0 + 0xB0, px + dx), WF(a0 + 0xB4, py + dy), WF(a0 + 0xB8, pz + dz),
                       W32(SCRATCH + 0xE4, k), W32(SCRATCH + 0x1E4, rng.choice((k, k, k + 1)))]
        out.append(case(f'1AA000 #{i}', 0x1AA000, B0, [a0, a1, SCRATCH, SCRATCH + 0x100], writes=w2))
    # 001B0300
    for beat in BEATS:
        ram, _ = image(beat)
        for sub2 in (None, 0, 1, 2):
            writes = [W8(0x8101E7, 0xA5)] + ([] if sub2 is None else [W8(0x810702, sub2)])
            out.append(case(f'1B0300 {beat} {sub2}', 0x1B0300, beat, [], writes=writes))
    # 001B6D70 / 001B76D0 on scratch records
    for op in (0, 1, 2, 3, 4, 5, 6, 7, 8, 0xFFFFFFFF):
        rec = [W32(SCRATCH + 8, op), W32(SCRATCH + 0x18, 0x1234 + op), WF(SCRATCH + 0x20, 77.5)]
        out.append(case(f'1B6D70 op {op:x}', 0x1B6D70, B0, [0x7B20F0, 0x55, SCRATCH], writes=rec))
    out.append(case('1B76D0', 0x1B76D0, B0, [1, 2, SCRATCH],
                    writes=[W32(SCRATCH + 0x14, 0x11), W32(SCRATCH + 0x18, 0xFFFFFFF0)]))
    # 001E7CB0
    for area in (0x13, 0x01):
        for sub2 in range(11):
            out.append(case(f'1E7CB0 {area:x}/{sub2}', 0x1E7CB0, B0, [],
                            writes=[W8(0x810700, area), W8(0x810702, sub2)]))
    return out


def cases_owners(rng):
    """00128C10, 00159B90, 0015A2C0 over their real records."""
    out = []
    for beat in BEATS:
        ram, _ = image(beat)
        for fn in (0x128C10, 0x159B90, 0x15A2C0, 0x158D30):
            for a in owners(beat, fn)[:3]:
                ind = u32(ram, a + 0x4C)
                out.append(case(f'{fn:06X} {beat} {a:x} as captured', fn, beat, [a], indirect=[ind],
                                script={0x1B2140: [1], 0x1B17A0: [1]}))
    B0 = BEATS[0]
    ram, _ = image(B0)
    # 00159B90
    p = owners(B0, 0x159B90)[0]
    ind = u32(ram, p + 0x4C)
    for st in range(5):
        for sub in range(5):
            for rv in (0, 1, 2, 3):
                if st != 1 and (sub or rv > 1):
                    continue
                for b4 in (0, 4, 5):
                    if (st != 1 or sub != 0) and b4:
                        continue
                    writes = [W8(p + 4, st), W8(p + 5, sub), W8(p + 0x1F0 + 0xB, b4), W8(p + 0x1F0 + 3, 0x38),
                              W8(0x810C7F, rv & 1), W16(p + 0x1F0 + 0x34, 1), W16(0x810CB2, 5)]
                    script = {0x1B0FD0: [rv & 1], 0x1BA1F0: [rv & 1], 0x1B17A0: [1]}
                    out.append(case(f'159B90 s{st} sub{sub} r{rv} b{b4}', 0x159B90, B0, [p], script=script,
                                    writes=writes, indirect=[ind]))
    # 0015A2C0
    ps = owners(B0, 0x15A2C0)
    for i in range(RM.pick(160, 160)):
        p = ps[i % len(ps)]
        st = rng.choice((0, 0, 1, 1, 1, 1, 2, 3, 4))
        link = rng.choice((0, 1, 2, 3))
        sub = rng.choice((0, 1, 2, 3))
        f20 = rng.choice((0.0, 1.0, 0.5, 99.0, 100.0, 98.5, 119.5, 120.0, 121.0, 60.0, -3.0))
        writes = [W8(p + 4, st), W16(p + 0x56, link), W8(p + 5, sub), WF(p + 0x20, f20),
                  W8(p + 0xA, rng.choice((0, 1))), W16(p + 0x2E, rng.choice((0, 2, 3, 4, 7))),
                  W16(p + 0x54, rng.choice((0, 1, 2, 3))), W32(0x70003B68, rng.getrandbits(32)),
                  W32(0x70003B64, rng.choice((0, 0x40, 0x80, 0x81, 0xC0, 0x100))),
                  W8(0x8106EC, rng.getrandbits(8)), W8(0x8106ED, rng.getrandbits(8))]
        script = {0x15A200: [rng.choice((0, 1)), rng.choice((0, 1))]}
        out.append(case(f'15A2C0 #{i} s{st} l{link} sub{sub} f{f20}', 0x15A2C0, B0, [p], script=script,
                        writes=writes))
        out[-1]['core'] = i < 60
    # 00128C10
    es = owners(B0, 0x128C10)
    for i in range(RM.pick(420, 420)):
        e = es[i % len(es)]
        b = e + 0x1F0
        st = rng.choice((0, 1, 1, 1, 1, 1, 1, 2, 3, 4, 4, 5))
        sub = rng.choice((0, 1, 2, 3, 4, 5, 8, 8, 9)) if st == 1 else rng.choice((0, 1, 2, 3, 4))
        kind = rng.choice((0, 1, 3, 4, 5, 8, 9, 10))
        px, py, pz = struct.unpack_from('<3f', ram, 0x810350)
        dist = rng.choice((0.0, 5.0, 15.0, 22.0, 30.0, 45.0, 120.0, 200.0))
        ang = rng.uniform(-3.14, 3.14)
        import math
        writes = [W8(e + 4, st), W8(e + 5, sub), W8(e + 6, rng.choice((0, 1, 2))), W8(e + 0xD, kind),
                  W8(e + 0xA, rng.choice((0, 1))), W8(e + 1, rng.choice((0, 1, 1))),
                  W8(b + 0xE0, rng.choice((0, 1))), W16(b + 0xD0, rng.choice((1, 1, 2, 89, 0x59, -1))),
                  W32(b + 0xE4, rng.choice((0x100, 0x200, 0))), W8(b + 0xFA, rng.choice((0, 1, 0x80))),
                  W8(0x70003B8D, rng.choice((0, 0, 1, 2))),
                  W32(0x70003B68, rng.choice((0, 0x40, 0x41, rng.getrandbits(32)))), W16(0x70003B8A, 0),
                  W16(e + 0x28, rng.choice((1, 2, 5))), W16(e + 0x36, rng.choice((0, 0, 3))),
                  W8(0x81080F, rng.choice((0, 0, 1))),
                  WF(e + 0xB0, px + dist * math.cos(ang)), WF(e + 0xB8, pz + dist * math.sin(ang)),
                  WF(e + 0xB4, rng.choice((py, -250.0, py + 1.0)))]
        if rng.random() < 0.5:
            writes.append(WF(b + 0xE8, struct.unpack_from('<f', ram, e + 0xC4)[0] + rng.choice((0.0, 0.01, 0.3, 1.0))))
        writes.append(W32(u32(ram, 0x24295C) + 0x58, random.Random(0x5EED + i).getrandbits(32)))
        script = {0x1B2140: [rng.choice((1, 1, 1, 0))], 0x128AB0: [rng.choice((0, 1))],
                  0x129780: [rng.choice((0, 1))], 0x1C25E0: [rng.choice((0, 1))],
                  0x1C2770: [rng.choice((0, 1)), rng.choice((0, 1))], 0x1B1630: [rng.choice((0, 1, 0x100))],
                  0x1C64F0: [rng.choice((0, 7, -2))]}
        out.append(case(f'128C10 #{i} s{st} sub{sub} k{kind}', 0x128C10, B0, [e], script=script, writes=writes,
                        indirect=[u32(ram, e + 0x4C)]))
    return out


def cases_world(rng):
    """0019B4C0 (and through it 001A06A0 / 0019CF50) over the captured
    world: probe segments around the real records."""
    out = []
    for beat in BEATS:
        ram, _ = image(beat)
        recs = [a for a, cb in pool(ram)]
        player = 0x8102B0
        for i in range(RM.pick(24, 24)):
            a0 = rng.choice(recs + [player])
            x, y, z = struct.unpack_from('<3f', ram, a0 + 0xB0)
            kind = rng.choice(('down', 'down', 'side', 'long', 'zero'))
            if kind == 'down':
                o = (x, y + rng.uniform(0.5, 4.0), z)
                t = (x + rng.uniform(-0.5, 0.5), y - rng.uniform(0.5, 6.0), z + rng.uniform(-0.5, 0.5))
            elif kind == 'side':
                o = (x, y + rng.uniform(0.2, 3.0), z)
                a = rng.uniform(-3.14, 3.14)
                d = rng.uniform(1.0, 12.0)
                import math
                t = (x + d * math.cos(a), o[1] + rng.uniform(-1, 1), z + d * math.sin(a))
            elif kind == 'long':
                o = (x + rng.uniform(-20, 20), y + rng.uniform(-5, 20), z + rng.uniform(-20, 20))
                t = (x + rng.uniform(-20, 20), y + rng.uniform(-5, 5), z + rng.uniform(-20, 20))
            else:
                o = t = (x, y + 1.0, z)
            flags = rng.choice((6, 0x80000006, 2, 4, 0, 0x80000004))
            writes = [WF(0x700038C0, t[0]), WF(0x700038C4, t[1]), WF(0x700038C8, t[2]), WF(0x700038CC, 1.0),
                      WF(0x700038D0, o[0]), WF(0x700038D4, o[1]), WF(0x700038D8, o[2]), WF(0x700038DC, 1.0)]
            out.append(case(f'19B4C0 {beat} #{i} {kind} {flags:x}', 0x19B4C0, beat,
                            [a0, 0x700038C0, 0x700038D0, flags], writes=writes))
            if i % 4 == 0:
                seg = [WF(0x70003190, o[0]), WF(0x70003194, o[1]), WF(0x70003198, o[2]),
                       WF(0x700031A0, t[0]), WF(0x700031A4, t[1]), WF(0x700031A8, t[2])]
                out.append(case(f'1A06A0 {beat} #{i}', 0x1A06A0, beat, [], writes=seg))
                out.append(case(f'19CF50 {beat} #{i}', 0x19CF50, beat, [], writes=seg))
    return out


def cases_fx(rng):
    """001E3D90 and 001E7D20 (with 001E7C60) over their real records."""
    out = []
    blocks = [SCRATCH + 0x10000 + 0x800 * k for k in range(40)]
    for beat in BEATS:
        ram, _ = image(beat)
        for p in owners(beat, 0x1E3D90)[:4]:
            out.append(case(f'1E3D90 {beat} {p:x} as captured', 0x1E3D90, beat, [p],
                            script={0x1CD070: [0x3001], 0x1CD2B0: [(0, FB(3.5))]}))
        for p in owners(beat, 0x1E7D20)[:2]:
            out.append(case(f'1E7D20 {beat} {p:x} as captured', 0x1E7D20, beat, [p],
                            script={0x1CB5F0: blocks, 0x1D2E00: [rng.choice((0, 1))]}))
    B0 = BEATS[0]
    ram, _ = image(B0)
    ps = owners(B0, 0x1E3D90)
    for i in range(RM.pick(90, 90)):
        p = ps[i % len(ps)]
        s3 = p + 0x1F0
        st = rng.choice((0, 1, 1, 1, 2, 3, 4))
        d = rng.choice((0, 1, 2, 2, 3))
        area, sub = rng.choice(((1, 0), (0, 0), (0, 1), (0x13, 1), (0x11, 0), (0xE, 0), (2, 2), (2, 0), (1, 1)))
        writes = [W8(p + 4, st), W8(p + 0xD, d), W8(0x8101E4, rng.choice((3, 3, 0, 1))), W8(0x810700, area),
                  W8(0x810701, sub), W8(0x810702, rng.choice((0, 5, 6, 7))),
                  WF(p + 0xB8, rng.choice((-800.0, -600.0, 0.0))), W32(s3, rng.choice((0, 0, 3))),
                  WF(s3 + 0x10, rng.choice((0.1, 1.999, 2.0, 1.995))), WF(s3 + 0x14, rng.choice((0.5, 1.99))),
                  WF(s3 + 0x18, rng.choice((0.5, 1.99))), W32(s3 + 8, rng.getrandbits(32)),
                  W32(0x275C00, rng.choice((0x100, 0x101, 0x40)))]
        script = {0x1CD070: [rng.choice((0xFFFFFF, 0x3001, 0x2F00))],
                  0x1CD2B0: [(0, rng.choice((0, FB(0.0), FB(12.5), FB(-0.0))))]}
        out.append(case(f'1E3D90 #{i} s{st} d{d} {area:x}/{sub}', 0x1E3D90, B0, [p], script=script, writes=writes,
                        fault=(d > 2 and st in (0, 1))))
        out[-1]['core'] = i < 60
    ps = owners(B0, 0x1E7D20)
    for i in range(RM.pick(24, 24)):
        p = ps[0]
        st = rng.choice((0, 0, 1, 1, 1, 2))
        area, sub = rng.choice(((1, 0), (0, 0), (0, 1), (0x13, 0), (0x13, 1), (6, 0)))
        d = rng.choice((0, 1)) if area == 0x13 else 0
        writes = [W8(p + 4, st), W8(p + 0xD, d), W8(0x810700, area), W8(0x810701, sub),
                  W8(p + 5, rng.choice((0, 1, 1, 2))), WF(p + 0xB4, rng.choice((200.0, 132.05, 131.0, 160.5, 150.0))),
                  W8(0x8107F5, rng.choice((0, 0xFF))), W8(0x8107F6, rng.choice((0, 0xFF))),
                  W8(0x810702, rng.choice((0, 4, 9))), W32(0x275C10, rng.choice((0, 3, 14, 20, -2))),
                  W32(0x275C14, rng.choice((0, 7))), W32(0x70003B68, rng.choice((0, 7, 9)))]
        if area == 0x13 and d == 1:
            # the record at D_00275C20 + 0xA060 must exist: keep it inside the overlay arena
            pass
        script = {0x1CB5F0: blocks, 0x1D2E00: [rng.choice((0, 1))]}
        out.append(case(f'1E7D20 #{i} s{st} {area:x}/{sub} d{d}', 0x1E7D20, B0, [p], script=script, writes=writes))
    for v in (0.0, 132.0, -5.5):
        out.append(case(f'1E7C60 {v}', 0x1E7C60, B0, [SCRATCH + 0x40000], fargs=[FB(v)]))
    return out


def cases_targeted(rng):
    """Enumerated paths the route records and random perturbations leave
    out (each named by the branch it takes)."""
    out = []
    B0 = BEATS[0]
    ram, _ = image(B0)
    px, py, pz = struct.unpack_from('<3f', ram, 0x810350)
    e = owners(B0, 0x128C10)[0]
    b = e + 0x1F0
    ind = u32(ram, e + 0x4C)
    c4 = struct.unpack_from('<f', ram, e + 0xC4)[0]

    def c128(name, st, sub, extra=(), script=None, kind=0, dist=500.0, xa=0, e1=1, seed=None):
        w = [W8(e + 4, st), W8(e + 5, sub), W8(e + 0xD, kind), W8(e + 0xA, xa), W8(e + 1, e1),
             W8(0x70003B8D, 0), W32(0x70003B68, 1), W16(0x70003B8A, 0), W16(e + 0x36, 0), W8(0x81080F, 0),
             WF(e + 0xB0, px + dist), WF(e + 0xB4, py), WF(e + 0xB8, pz), W8(b + 0xFA, 0)] + list(extra)
        if seed:
            w += seed_writes(ram, rng, seed)
        sc = {0x1B2140: [1]}
        sc.update(script or {})
        out.append(case('128C10 t ' + name, 0x128C10, B0, [e], script=sc, writes=w, indirect=[ind]))

    for kind in (0, 5):
        c128(f'sub0 k{kind} random heading', 1, 0, kind=kind, xa=1)
    for dist in (50.0, 150.0):
        c128(f'sub0 k0 d{dist}', 1, 0, dist=dist)
    for e4 in (0x100, 0x200):
        for dist in (15.0, 30.0, 50.0):
            c128(f'sub0 k5 e4 {e4:x} d{dist}', 1, 0, kind=5, dist=dist, extra=[W32(b + 0xE4, e4)])
    for g in (0, 1):
        c128(f'sub0 ground {g}', 1, 0, script={0x1C25E0: [g]})
    c128('sub0 no e1', 1, 0, e1=0)
    c128('sub1 random heading', 1, 1, xa=1)
    # the heading draw's bits 3..7: bit 3 set and clear, every bit 4..7
    # pattern class (the 0xF0 mask keeps bits 4..7 only)
    for sub in (0, 1):
        for bits in (0x08, 0xF8, 0x07, 0xA0, 0x58):
            c128(f'sub{sub} heading draw {bits:02x}', 1, sub, xa=1,
                 seed=[lambda v, bits=bits: (v & 0xFF) == bits])
    for bits in (0x18, 0xE8):
        c128(f'sub1 near heading draw {bits:02x}', 1, 1, dist=5.0, extra=[W32(b + 0xE4, 0x100)],
             seed=[lambda v, bits=bits: (v & 0xFF) == bits])
    # 'sub-state 0' stored at 90 idle frames must survive to the end: no
    # ground test (+1 clear) or a ground test that holds
    for d0 in (0x59, 0x58):
        c128(f'sub1 far d0 {d0:x} no ground test', 1, 1, dist=200.0, e1=0, extra=[W16(b + 0xD0, d0)])
        c128(f'sub1 far d0 {d0:x} ground holds', 1, 1, dist=200.0, extra=[W16(b + 0xD0, d0)],
             script={0x1C25E0: [1]})
    for d0 in (0x59, 3):
        c128(f'sub1 far d0 {d0:x}', 1, 1, dist=200.0, extra=[W16(b + 0xD0, d0)])
    for e4 in (0x100, 0x200):
        for dist in (5.0, 15.0, 20.0, 30.0):
            c128(f'sub1 near e4 {e4:x} d{dist}', 1, 1, dist=dist, extra=[W32(b + 0xE4, e4)])
    for g in (0, 1):
        c128(f'sub1 ground {g}', 1, 1, dist=100.0, extra=[W32(b + 0xE4, 0x200)], script={0x1C25E0: [g]})
    for off in (0.0, 0.2, 1.0):
        c128(f'sub2 e8 +{off}', 1, 2, kind=5, extra=[WF(b + 0xE8, c4 + off), WF(e + 0xC4, c4)])
    for e6 in (0, 1, 2):
        for d0 in (1, 5):
            for bit in (0, 0x10):
                for e1 in (0, 1):
                    if e6 != 0 and bit:
                        continue
                    c128(f'sub3 e6 {e6} d0 {d0} bit {bit:x} e1 {e1}', 1, 3, e1=e1,
                         extra=[W8(e + 6, e6), W16(b + 0xD0, d0), WF(b + 0xE8, 0.5)],
                         seed=[lambda v, bit=bit: (v & 0x10) == bit])
    for d0 in (1, 4):
        c128(f'sub4 d0 {d0}', 1, 4, extra=[W16(b + 0xD0, d0)])
    for r in (0, 1):
        c128(f'sub8 r{r}', 1, 8, script={0x1C2770: [r, 0]})
    for kind in (4, 9):
        c128(f'kind {kind}', 1, 0, kind=kind)
    for fa in (0x80, 1):
        c128(f'fa {fa:x}', 1, 5, extra=[W8(b + 0xFA, fa)], script={0x128B80: []})
    c128('kind 10', 1, 0, kind=10)
    for b8d in (1, 2):
        c128(f'b8d {b8d}', 1, 5, extra=[W8(0x70003B8D, b8d)])
    for y in (-250.0, 0.0):
        for st in (1, 2):
            c128(f'64th st{st} y{y}', st, 5, extra=[W32(0x70003B68, 0x40), WF(e + 0xB4, y)])
    # frame words whose low five bits are 0 but not the low six (not a 64th
    # frame), and a halfword 0x70003B8A that completes one
    for st in (1, 2):
        for fw, c8a in ((0x20, 0), (0x60, 0), (0x1E, 2), (0x3E, 2)):
            c128(f'not 64th st{st} {fw:x}+{c8a}', st, 5,
                 extra=[W32(0x70003B68, fw), W16(0x70003B8A, c8a), WF(e + 0xB4, -250.0)])
    c128('128B80 busy', 1, 0, extra=[W16(e + 0x36, 2)])
    for r in (0, 1):
        c128(f'st0 sub0 r{r}', 0, 0, script={0x128AB0: [r]})
        for kind in (0, 4, 9):
            c128(f'st0 sub1 r{r} k{kind}', 0, 1, kind=kind, script={0x129780: [r]})
    c128('st0 sub2', 0, 2)
    for e0 in (0, 1):
        c128(f'st3 e0 {e0}', 3, 0, extra=[W8(b + 0xE0, e0)])
    c128('st4 sub0', 4, 0)
    for r in (0, 1):
        c128(f'st4 sub1 r{r}', 4, 1, script={0x129780: [r]})
    for c28 in (1, 3):
        c128(f'st4 sub2 {c28}', 4, 2, extra=[W16(e + 0x28, c28)])
    for dist in (50.0, 200.0):
        for g in (0, 0x100, 1):
            c128(f'st4 sub3 d{dist} g{g:x}', 4, 3, dist=dist, script={0x1B1630: [g]})
    c128('st4 sub4', 4, 4)
    c128('st5', 5, 0)
    # round 4: the byte 0x70003B8D with one bit set at a time (the original
    # tests it whole, then masked to eight bits); state 0 sub-state 1 with
    # each kind around 4 and 9 and the byte +0 poisoned (its store is
    # conditional on the kind); state 1 with +5 = 8, 9, 0x80 (the 'below 9'
    # test); state 4 sub-state 3 with 001B1630 returning one bit at a time
    # (its low byte is tested)
    for xa in (2, 6, 0xFE):
        for sub in (0, 1):
            c128(f'sub{sub} xa {xa:x}', 1, sub, xa=xa)
    for b8d in (4, 8, 0x10, 0x20, 0x40, 0x80):
        c128(f'b8d bit {b8d:x}', 1, 5, extra=[W8(0x70003B8D, b8d)])
    for kind in (3, 4, 5, 8, 9, 10, 0x84):
        c128(f'st0 sub1 kind {kind:x}', 0, 1, kind=kind, script={0x129780: [1]},
             extra=[W8(e, 0x5A), W16(e + 0x54, 0x5A5A)])
    for sub in (8, 9, 0x80):
        c128(f'sub {sub:x} gate', 1, sub, extra=[W16(b + 0xD0, 3)])
    for g in (1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x100):
        c128(f'st4 sub3 d200 g bit {g:x}', 4, 3, dist=200.0, script={0x1B1630: [g]},
             extra=[W8(e, 0x5A), W8(e + 6, 0x5A), W8(e + 7, 0x5A)])

    # 00159B90 sub 0 with each 00157CE0 result (it reads the record p itself)
    p = owners(B0, 0x159B90)[0]
    pind = u32(ram, p + 0x4C)
    for name, w in (('r0', [W8(p + 0xB, 0)]),
                    ('r2', [W8(p + 0xB, 4), W8(p + 3, 0x38), W8(0x810C7F, 0)]),
                    ('r1', [W8(p + 0xB, 4), W8(p + 3, 0x38), W8(0x810C7F, 1), W16(p + 0x34, 1), W16(0x810CB2, 5)]),
                    ('r3', [W8(p + 0xB, 5), W8(p + 3, 0x12), W8(0x810C63, 2), W16(0x810CB4, 61)])):
        out.append(case(f'159B90 t sub0 {name}', 0x159B90, B0, [p], writes=[W8(p + 4, 1), W8(p + 5, 0)] + w,
                        script={0x1B17A0: [1]}, indirect=[pind]))

    # 0015A2C0 link 1 / link 2 paths
    q = owners(B0, 0x15A2C0)[0]
    out.append(case('15A2C0 t link1 sub0 idle', 0x15A2C0, B0, [q],
                    writes=[W8(q + 4, 1), W16(q + 0x56, 1), W8(q + 5, 0), W8(q + 0xA, 0), WF(q + 0x20, 7.0)]))
    for f20 in (119.0, 120.5):
        for r in (0, 1):
            for c2e in (2, 3, 5):
                out.append(case(f'15A2C0 t link2 f{f20} r{r} 2e{c2e}', 0x15A2C0, B0, [q],
                                writes=[W8(q + 4, 1), W16(q + 0x56, 2), W8(q + 5, 0), W8(q + 0xA, 1),
                                        WF(q + 0x20, f20), W16(q + 0x2E, c2e)], script={0x15A200: [r]}))

    # 00158590 with a zero direction at +0xF0: the dot product is exactly
    # 0.0, the boundary of the 'negative: return' test. For -0.0 every
    # product must be -0: the local vector D_008105D0 - p+0xB0 has every
    # component +1 (direction -0) or -1 (direction +0) (with mixed signs the
    # VU0 sum is +0; build/area01/sys/r6 checks the dot is 0x80000000)
    p = owners(B0, 0x159B90)[0]
    cx = struct.unpack_from('<3f', ram, 0x8105D0)
    for mode in (-1, 1, 0, 2):
        out.append(case(f'158590 t zero dot m{mode}', 0x158590, B0, [p, 1, mode & MASK],
                        writes=[WF(p + 0xF0, 0.0), WF(p + 0xF4, 0.0), WF(p + 0xF8, 0.0), WF(p + 0xBC, 3.5)]))
        for tag, sgn, d in (('', 1.0, -0.0), (' n', -1.0, 0.0)):
            out.append(case(f'158590 t negative zero dot{tag} m{mode}', 0x158590, B0, [p, 0, mode & MASK],
                            writes=[WF(p + 0xB0 + 4 * k, cx[k] - sgn) for k in range(3)] +
                            [WF(p + 0xF0, d), WF(p + 0xF4, d), WF(p + 0xF8, d)]))

    # 00159B90 sub-states 2 and 3 with the bytes they clear poisoned
    pind = u32(ram, p + 0x4C)
    for sub in (2, 3):
        for rv in (0, 1):
            out.append(case(f'159B90 t sub{sub} r{rv} poisoned', 0x159B90, B0, [p],
                            writes=[W8(p + 4, 1), W8(p + 5, sub), W8(p + 0xB, 0xA5), W8(p, 0x5A)],
                            script={0x1BA1F0: [rv], 0x1B17A0: [1]}, indirect=[pind]))

    # 00158D30 state 0 with +0x80..+0x88 poisoned (each 2.0 store observable)
    p = owners(B0, 0x158D30)[0]
    out.append(case('158D30 t st0 poisoned', 0x158D30, B0, [p],
                    writes=[W8(p + 4, 0), WF(p + 0x80, 7.0), WF(p + 0x84, -7.0), WF(p + 0x88, 0.5)],
                    indirect=[u32(ram, p + 0x4C)]))

    # 0015A2C0 state 0 with every field it clears poisoned, each link mode
    q = owners(B0, 0x15A2C0)[0]
    for link in (0, 1, 2):
        out.append(case(f'15A2C0 t st0 poisoned link{link}', 0x15A2C0, B0, [q],
                        writes=[W8(q + 4, 0), W16(q + 0x56, link), W8(q + 0xB, 0xA5), W8(q + 0xA, 0x5A),
                                W8(q + 0xC, 0x3C), W8(q + 9, 0xC3), W32(q + 0x80, 0x12345678), W32(q + 0x20, FB(9.0)),
                                W16(q + 0x2E, 0x77), W32(q + 0x1F0, 0xDEADBEEF), W8(q, 0x66)],
                        script={0x15A200: [1, 0]}))

    # 001A9E00 contact with a1's byte +0x0B clear, a0's byte +0 with and
    # without bit 2 (value 4) and with bit 1 (value 2) alone
    a0r, a1r = owners(B0, 0x128C10)[2], owners(B0, 0x128C10)[3]
    x1, y1, z1 = struct.unpack_from('<3f', ram, a1r + 0xB0)
    h0 = struct.unpack_from('<f', ram, u32(ram, a0r + 0x30) + 4)[0] / 2
    for b0 in (4, 5, 2, 0, 6):
        out.append(case(f'1A9E00 t contact a0 byte {b0}', 0x1A9E00, B0, [a0r, a1r],
                        writes=[WF(a0r + 0xA0, x1 + 0.25), WF(a0r + 0xA4, y1 - h0), WF(a0r + 0xA8, z1 - 0.125),
                                W8(a1r + 0xB, 0), W8(a1r + 3, 0), W8(a0r, b0)]))

    # 001AA000 height test at its rounding edge: radii 1.0 and 0.75 ulp(1.0),
    # whose exact sum is not a float (the EE add truncates it to 1.0), and a
    # height gap of 1.0 + k ulps
    for k in range(0, 5):
        ra, rb = owners(B0, 0x128C10)[4], owners(B0, 0x128C10)[5]
        ea, eb = SCRATCH + 0x200, SCRATCH + 0x220            # private extents (the records share theirs)
        w = [W32(ra + 0x30, ea), W32(rb + 0x30, eb), WF(ea, 1.0), WF(eb, 1.0),
             W32(ea + 4, FB(1.0)), W32(eb + 4, 0x33C00000),
             W32(ra + 0xB0, u32(ram, rb + 0xB0)), W32(ra + 0xB8, u32(ram, rb + 0xB8)),
             WF(rb + 0xB4, 0.0), W32(ra + 0xB4, 0x3F800000 + k),
             W32(SCRATCH + 0xE4, 0x100), W32(SCRATCH + 0x1E4, 0x100)]
        out.append(case(f'1AA000 t height edge +{k}', 0x1AA000, B0, [ra, rb, SCRATCH, SCRATCH + 0x100], writes=w))

    # 001A8840: the height test failing, and a0's byte +0 not 1
    a1 = owners(B0, 0x128C10)[1]
    x1, y1, z1 = struct.unpack_from('<3f', ram, a1 + 0xB0)
    for name, dy, a00, f707 in (('y fail', 50.0, 1, 0), ('a0 not 1', 0.0, 2, 0), ('a0 is 1', 0.0, 1, 0),
                                ('a0 is 1, 707 = 1', 0.0, 1, 1), ('a0 is 1, 707 = 2', 0.0, 1, 2)):
        out.append(case(f'1A8840 t {name}', 0x1A8840, B0, [0x8102B0, a1],
                        writes=[WF(0x8102B0 + 0xA0, x1), WF(0x8102B0 + 0xA4, y1 + dy), WF(0x8102B0 + 0xA8, z1),
                                W8(a1 + 0xD, 0), W8(a1 + 0xB, 1), W8(0x810707, f707), W8(0x8102B0, a00)]))

    # 001E3D90: area key 0x100 with D_008101E4 == 3 and +0xB8 below -700
    r0 = owners(B0, 0x1E3D90)[0]
    for y in (-800.0, -600.0):
        out.append(case(f'1E3D90 t key100 y{y}', 0x1E3D90, B0, [r0],
                        writes=[W8(r0 + 4, 1), W8(0x8101E4, 3), W8(0x810700, 1), W8(0x810701, 0), WF(r0 + 0xB8, y)],
                        script={0x1CD070: [0x3001], 0x1CD2B0: [(0, FB(2.0))]}))

    # 001E3D90: seeds whose products carry across bit 16 (the three texture
    # offsets), negative seeds, and a seed of zero
    def carry(v):
        return (v * 37 + 11) & 0xFFFF == 0xFFFF
    seeds = [0, 0x80000000, 0xFFFFFFFF, 0x7FFFFFFF]
    for _ in range(4000):
        x = rng.getrandbits(32)
        if carry(x):
            seeds.append(x)
            break
    for _ in range(200000):
        x = rng.getrandbits(32)
        if carry((x * 37 + 11) & MASK):
            seeds.append(x)
            break
    for sd in seeds:
        out.append(case(f'1E3D90 t seed {sd:08x}', 0x1E3D90, B0, [r0],
                        writes=[W8(r0 + 4, 1), W8(r0 + 0xD, 1), W32(r0 + 0x1F0 + 8, sd), W8(0x810700, 1)],
                        script={0x1CD070: [0x3001], 0x1CD2B0: [(0, FB(2.0))]}))

    # 001E7D20: every state / area / record / sub-state combination
    w0 = owners(B0, 0x1E7D20)[0]
    blocks = [SCRATCH + 0x10000 + 0x800 * k for k in range(40)]
    for st in (2, 3, 4):
        out.append(case(f'1E7D20 t st{st}', 0x1E7D20, B0, [w0], writes=[W8(w0 + 4, st)]))
    for area, sub, d in ((1, 0, 0), (0, 0, 0), (0, 1, 0), (6, 0, 0), (2, 0, 0), (0x13, 0, 0), (0x13, 0, 1),
                         (0x13, 0, 2)):
        for flag in (0, 0xFF):
            if area != 0x13 and flag:
                continue
            w = [W8(w0 + 4, 0), W8(w0 + 0xD, d), W8(0x810700, area), W8(0x810701, sub), W8(0x8107F5, flag),
                 W8(0x8107F6, flag), WF(w0 + 0xB0, 920.0), WF(w0 + 0xB8, 900.0), WF(w0 + 0xC0, 100.0),
                 WF(w0 + 0xC8, 60.0), WF(w0 + 0xB4, 140.0)]
            out.append(case(f'1E7D20 t init {area:x}/{sub} d{d} f{flag:x}', 0x1E7D20, B0, [w0], writes=w))
    for d in (0, 1, 2):
        for p5 in (0, 1, 2, 3):
            for y, flag in ((200.0, 0), (100.0, 0), (200.0, 0xFF)):
                if p5 != 1 and (y, flag) != (200.0, 0):
                    continue
                w = [W8(w0 + 4, 1), W8(w0 + 0xD, d), W8(0x810700, 0x13), W8(0x810701, 0), W8(w0 + 5, p5),
                     WF(w0 + 0xB4, y), W8(0x8107F5, flag), W8(0x8107F6, flag), W8(0x810702, 0)]
                out.append(case(f'1E7D20 t 13 d{d} p5 {p5} y{y} f{flag:x}', 0x1E7D20, B0, [w0], writes=w,
                                script={0x1CB5F0: blocks, 0x1D2E00: [p5 & 1]}))
    for row, col in ((0, 0), (31, 31), (0, 31), (31, 0), (5, 17)):
        w = [W8(w0 + 4, 1), W8(0x810700, 1)] + seed_writes(ram, rng, [cell_is(row), cell_is(col)])
        out.append(case(f'1E7D20 t splash {row},{col}', 0x1E7D20, B0, [w0], writes=w,
                        script={0x1CB5F0: blocks}))
    for c14, c10 in ((7, 20), (7, -2), (9, 5), (9, 20)):
        w = [W8(w0 + 4, 1), W8(0x810700, 0x13), W8(w0 + 0xD, 2), W8(0x810702, 0), W32(0x70003B68, 9),
             W32(0x275C14, c14), W32(0x275C10, c10 & MASK)]
        out.append(case(f'1E7D20 t table 14={c14} 10={c10}', 0x1E7D20, B0, [w0], writes=w,
                        script={0x1CB5F0: blocks}, fault=(c14 == 9 and c10 == 20)))
    # the packet blocks poisoned, so every word the original writes into
    # them is observable
    poison = [(blk, bytes([0xA5]) * 0x800) for blk in blocks]
    for area in (0x13, 1, 6):
        out.append(case(f'1E7D20 t packets poisoned {area:x}', 0x1E7D20, B0, [w0], script={0x1CB5F0: blocks},
                        writes=poison + [W8(w0 + 4, 1), W8(0x810700, area), W8(w0 + 0xD, 0), W8(0x810702, 0)]))
    out.append(case('1E7D20 t gate closed', 0x1E7D20, B0, [w0],
                    writes=[W8(w0 + 4, 1), W8(0x810700, 0x13), W8(w0 + 0xD, 2), W8(0x810702, 4)]))
    return out


def cases_probe(rng):
    """0019B4C0 inputs the route records leave out: a type byte (+2) with
    bits above the low four set (the halfword 0x7000324E keeps five), and a
    first point that is the scratch segment itself (a1 = 0x70003190: the
    original reads a1[i] again after storing 0x70003190[i])."""
    out = []
    B0 = BEATS[0]
    ram, _ = image(B0)
    a0 = owners(B0, 0x128C10)[0]
    x, y, z = struct.unpack_from('<3f', ram, a0 + 0xB0)
    seg = [WF(0x700038C0, x + 0.3), WF(0x700038C4, y - 2.0), WF(0x700038C8, z + 0.2), WF(0x700038CC, 1.0),
           WF(0x700038D0, x), WF(0x700038D4, y + 2.0), WF(0x700038D8, z), WF(0x700038DC, 1.0)]
    for t in (0x14, 0xF4, 0x1F, 0x0F):
        out.append(case(f'19B4C0 t type byte {t:x}', 0x19B4C0, B0, [a0, 0x700038C0, 0x700038D0, 6],
                        writes=seg + [W8(a0 + 2, t)]))
    for flags in (6, 0x80000006, 4):
        out.append(case(f'19B4C0 t aliased first point {flags:x}', 0x19B4C0, B0, [a0, 0x70003190, 0x700038D0, flags],
                        writes=seg + [WF(0x70003190, x + 0.3), WF(0x70003194, y - 2.0), WF(0x70003198, z + 0.2),
                                      WF(0x7000319C, 1.0)]))
    return out


def cases_synthetic(rng):
    """001A06A0 and 0019CF50 over synthetic lists, boxes, cells and faces
    written into zeroed RAM (test inputs, not disc data), with their leaf
    tests scripted (forced stubs) so that every walker branch is taken."""
    out = []
    B0 = BEATS[0]
    S2 = SCRATCH + 0x60000
    L, T, R = S2, S2 + 0x2000, S2 + 0x100
    w = [WF(0x70003190, 0.0), WF(0x70003194, 12.0), WF(0x70003198, 0.0),
         WF(0x700031A0, 10.0), WF(0x700031A4, 2.0), WF(0x700031A8, 10.0),
         WF(0x700031B0, 5.0), WF(0x700031B4, 6.0), WF(0x700031B8, 4.0),
         W32(0x70003250, T), W16(0x7000324C, 6), W32(0x275B7C, L), W16(0x275B84, 11)]
    boxes = {2: (30.0, 30.0, 30.0, 40.0, 40.0, 40.0), 3: (-1.0, -1.0, -1.0, 20.0, 20.0, 20.0),
             4: (-1.0, -1.0, -1.0, 20.0, 20.0, 20.0), 5: (-1.0, -1.0, -1.0, 20.0, 20.0, 20.0),
             0: (-1.0, 15.0, -1.0, 20.0, 20.0, 20.0)}
    shapes = [(0x1000, 1, 0x14 + 0x18), (0x1800, 2, 0x24 + 2 * 0x30), (0x2000, 0, 0x1C), (0x4000, 0, 0x18),
              (0x4800, 0, 0x2C), (0x8000, 0, 0x14), (0x8800, 0, 0x24), (0x0000, 0, 0)]
    off = 0x100
    for idx, bx in boxes.items():
        base = T + off
        w.append(W32(T + 4 + idx * 4, off))
        w += [WF(base + 4 * k, v) for k, v in enumerate(bx)]
        w.append(W16(base + 0x18, len(shapes)))
        at = base + 0x1C
        for kind, n, size in shapes:
            w += [W16(at, kind), W8(at + 2, n)]
            at += size
        off += 0x400
    # records: (byte0, type, box index, +0x54)
    recs = [(0, 4, 3, 0), (1, 3, 3, 0), (1, 4, 3, 0), (1, 4, 0xFF, 0), (1, 4, 1, 0), (1, 4, 7, 0), (1, 4, 2, 0),
            (1, 4, 3, 0x50), (1, 4, 4, 0x21), (1, 4, 5, 0x22), (1, 4, 0, 0x23)]
    for k, (b0, typ, idx, k54) in enumerate(recs):
        a = R + k * 0x100
        w += [W32(L + 4 * k, a), W8(a, b0), W8(a + 2, typ), W16(a + 0xE, idx << 8), W8(a + 0x54, k54)]
    w.append(W32(0x70003254, R + 2 * 0x100))
    w.append(W32(T + 4 + 7 * 4, 0x100))       # index 7 has a box but is not below the count
    forced = (0x1A4030, 0x1A50A0, 0x1A5C30)
    for hit in (0, 1):
        script = {0x1A4030: [0, 0, 0, 0, 0, 0], 0x1A50A0: [0, 0, 0],
                  0x1A5C30: [0, 0, 0, hit, 0, 0, 0, 0, 0, 0, 0, 1]}
        out.append(case(f'1A06A0 s synthetic hit{hit}', 0x1A06A0, B0, [], writes=w, script=script, forced=forced))
    w2 = w + [WF(0x70003194, 0.0), WF(0x700031A4, 12.0), WF(0x70003198, 10.0), WF(0x700031A8, 0.0),
              WF(0x70003190, 10.0), WF(0x700031A0, 0.0)]
    out.append(case('1A06A0 s synthetic reversed', 0x1A06A0, B0, [], writes=w2, forced=forced,
                    script={0x1A5C30: [0, 0, 0, 1, 0, 0, 0, 1]}))

    # 0019CF50
    C0, N, F = SCRATCH + 0x70000, SCRATCH + 0x71000, SCRATCH + 0x72000
    for name, col2, best0, hits in (('hit', 3, 0x7FFF, [1, 0]), ('miss', 3, 0x7FFF, [0, 0]),
                                    ('empty span', 6, 0x7FFF, []), ('no span', 3, -5, None),
                                    ('no span best 3', 3, 3, None)):
        w = [W16(0x70003240 + 2 * i, 5) for i in range(6)]
        cols = [0, 20, col2, 30, 0, 30]
        for i in range(6):
            w += [W32(0x70003228 + 4 * i, C0 + i * 0x100), W16(C0 + i * 0x100 + 10, cols[i]),
                  W32(0x70003210 + 4 * i, N)]
        w += [W32(0x7000320C, best0 & MASK), W32(0x70003208, F), W32(0x700031D0, 0x1234),
              WF(0x70003190, 1.0), WF(0x700031A0, 2.0), WF(0x70003194, 3.0), WF(0x700031A4, 1.0),
              WF(0x70003198, 1.0), WF(0x700031A8, 1.0), WF(0x700031B0, 7.0), WF(0x700031B4, 8.0),
              WF(0x700031B8, 9.0)]
        for slot, (face, kind) in enumerate(((0, 0x52), (1, 0x55), (2, 0x10))):
            a = F + face * 64
            w += [W16(N + (3 + slot) * 2, face), W16(a + 0xC, 0), W16(a + 0xE, 10), W16(a + 0x10, 0),
                  W16(a + 0x12, 10), W16(a + 0x14, 0), W16(a + 0x16, 10), W8(a + 0x1A, kind)]
        out.append(case(f'19CF50 s synthetic {name}', 0x19CF50, B0, [], writes=w, forced=(0x19F1A0, 0x19ED80),
                        script={0x19ED80: hits or []}, fault=hits is None))
        if name == 'hit':
            # every kind boundary of the face filter (0x50, 0x51, 0x53, 0x54, 0x59, 0x5A), and a face
            # box failing each of its six halfword tests
            wk = list(w) + [W16(C0 + 2 * 0x100 + 10, -3 & 0xFFFF), W16(C0 + 10, -10 & 0xFFFF),
                            W16(C0 + 4 * 0x100 + 10, -10 & 0xFFFF)]      # best span: -3 .. 6, nine faces
            kinds = (0x50, 0x51, 0x53, 0x54, 0x59, 0x5A, 0xFF, 0x00, 0x7F)
            for slot, kind in enumerate(kinds):
                a = F + (8 + slot) * 64
                wk += [W16(N + (slot - 3) * 2, 8 + slot),
                       W16(a + 0xC, 0), W16(a + 0xE, 10), W16(a + 0x10, 0), W16(a + 0x12, 10),
                       W16(a + 0x14, 0), W16(a + 0x16, 10), W8(a + 0x1A, kind)]
            out.append(case('19CF50 s synthetic kinds', 0x19CF50, B0, [], writes=wk, forced=(0x19F1A0, 0x19ED80),
                            script={0x19ED80: [0, 1, 0, 1, 0, 1, 0, 1, 0]}))
            # a negative bound from 0019F1A0 (a signed halfword): its column
            # entry lies before the column table start
            for bi in (1, 2):
                wn = list(w) + [W16(0x70003240 + 2 * bi, -2 & 0xFFFF), W16(C0 + bi * 0x100 - 4, 7),
                                W16(C0 + bi * 0x100 - 2, 9)]
                out.append(case(f'19CF50 s synthetic negative bound {bi}', 0x19CF50, B0, [], writes=wn,
                                forced=(0x19F1A0, 0x19ED80), script={0x19ED80: [1, 1, 1]}))
            for k, (off, val) in enumerate(((0xC, 6), (0xE, 4), (0x10, 6), (0x12, 4), (0x14, 6), (0x16, 4))):
                wb = list(w) + [W16(F + 64 + off, val)]
                out.append(case(f'19CF50 s synthetic box test {k}', 0x19CF50, B0, [], writes=wb,
                                forced=(0x19F1A0, 0x19ED80), script={0x19ED80: [1, 1, 1]}))
    return out


def cases_edges(rng):
    """Inputs the other generators leave out, each making one original
    operation observable (a whole-byte test, a halfword store, a mask, a
    signed shift or compare, a strict float bound): a translation with the
    narrower or wider operation gives a different result on it."""
    out = []
    B0 = BEATS[0]
    ram, _ = image(B0)
    poison_b0 = [W32(0x700038B0 + 4 * i, 0xA5A5A5A5) for i in range(8)]
    # 00158D30 state 1, halfword +0x2E = 0: the test is 'byte +0x0B != 0'
    # (the whole byte), so values with bit 0 clear must still select the
    # second word set
    p = owners(B0, 0x158D30)[0]
    ind = u32(ram, p + 0x4C)
    for bb in (2, 4, 0x80, 0xFE):
        out.append(case(f'158D30 t st1 2e0 byte0B {bb:x}', 0x158D30, B0, [p],
                        writes=[W8(p + 4, 1), W16(p + 0x2E, 0), W8(p + 0xB, bb)] + poison_b0,
                        script={0x1B17A0: [1]}, indirect=[ind]))
    # 00159B90 state 0 with every field it stores poisoned (the +0x34 store
    # is a halfword: +0x35 must be cleared too)
    q = owners(B0, 0x159B90)[0]
    qind = u32(ram, q + 0x4C)
    out.append(case('159B90 t st0 poisoned', 0x159B90, B0, [q],
                    writes=[W8(q + 4, 0), W8(q + 0xA, 0x5A), W32(q + 0x30, 0x12345678), W16(q + 0x34, 0xA5A5),
                            W8(q, 0x66)] + [W32(0x700038A0 + 4 * i, 0xC3C3C3C3) for i in range(4)],
                    script={0x1B0FD0: [0]}, indirect=[qind]))
    # 001A06A0: list records whose type byte +2 has bit 4 (or bits 4..5)
    # set above a low nibble of 4; the original masks with 0x1F, so 0x14
    # and 0x34 are not type 4 (skipped) and 0x24 / 0xE4 are
    S2 = SCRATCH + 0x68000
    L, T, R = S2, S2 + 0x2000, S2 + 0x100
    seg = [WF(0x70003190, 0.0), WF(0x70003194, 12.0), WF(0x70003198, 0.0),
           WF(0x700031A0, 10.0), WF(0x700031A4, 2.0), WF(0x700031A8, 10.0),
           W32(0x70003250, T), W16(0x7000324C, 6), W32(0x275B7C, L), W32(0x70003254, 0)]
    box = T + 0x100
    w = seg + [W32(T + 4 + 3 * 4, 0x100)] + [WF(box + 4 * k, v) for k, v in
                                               enumerate((-1.0, -1.0, -1.0, 20.0, 20.0, 20.0))]
    w += [W16(box + 0x18, 1), W16(box + 0x1C, 0x2000), W8(box + 0x1E, 0)]
    for typ in (0x14, 0x34, 0x24, 0xE4):
        a = R
        wt = w + [W16(0x275B84, 1), W32(L, a), W8(a, 1), W8(a + 2, typ), W16(a + 0xE, 3 << 8), W8(a + 0x54, 0)]
        out.append(case(f'1A06A0 s type byte {typ:x}', 0x1A06A0, B0, [], writes=wt, forced=(0x1A4030, 0x1A50A0, 0x1A5C30),
                        script={0x1A50A0: [1]}))
    # 001A06A0: box indices 0xFE and 0xFF with a box table of 0x100 entries
    # (the index 0xFF is skipped as 'no box' before the count test; 0xFE is
    # a real box)
    for idx in (0xFE, 0xFF):
        a = R
        wt = seg + [W16(0x7000324C, 0x100), W32(T + 4 + idx * 4, 0x800)]
        wt += [WF(T + 0x800 + 4 * k, v) for k, v in enumerate((-1.0, -1.0, -1.0, 20.0, 20.0, 20.0))]
        wt += [W16(T + 0x800 + 0x18, 1), W16(T + 0x800 + 0x1C, 0x2000), W8(T + 0x800 + 0x1E, 0)]
        wt += [W16(0x275B84, 1), W32(L, a), W8(a, 1), W8(a + 2, 4), W16(a + 0xE, idx << 8), W8(a + 0x54, 0)]
        out.append(case(f'1A06A0 s box index {idx:x}', 0x1A06A0, B0, [], writes=wt,
                        forced=(0x1A4030, 0x1A50A0, 0x1A5C30), script={0x1A50A0: [1]}))
    # 001AA000 push-out at eight headings with a radius sum that is not a
    # power of two, so the two products (sum * sin, sum * cos) are inexact
    # and the EE multiply's rounding shows; the records sit at the origin so
    # the final add keeps every bit of the products
    import math
    ra, rb = owners(B0, 0x128C10)[4], owners(B0, 0x128C10)[5]
    ea, eb = SCRATCH + 0x240, SCRATCH + 0x260
    for k in range(8):
        th = 0.3 + k * 0.77
        w = [W32(ra + 0x30, ea), W32(rb + 0x30, eb), WF(ea, 0.7), WF(eb, 0.6), WF(ea + 4, 1.0), WF(eb + 4, 1.0),
             WF(rb + 0xB0, 0.0), WF(rb + 0xB8, 0.0),
             WF(ra + 0xB0, 0.5 * math.cos(th)), WF(ra + 0xB8, 0.5 * math.sin(th)),
             WF(ra + 0xB4, 3.0), WF(rb + 0xB4, 3.0), W32(SCRATCH + 0xE4, 0x100), W32(SCRATCH + 0x1E4, 0x100)]
        out.append(case(f'1AA000 t push heading {k}', 0x1AA000, B0, [ra, rb, SCRATCH, SCRATCH + 0x100], writes=w))
    # 00158590 mode 2 at headings whose cosine / sine times 1.5 is inexact
    # (the captured records face heading 0: exact products)
    p = owners(B0, 0x159B90)[0]
    for yaw in (0.37, 1.3, 2.9, -2.2):
        out.append(case(f'158590 t mode 2 yaw {yaw}', 0x158590, B0, [p, 0, 2],
                        writes=[WF(p + 0xC4, yaw), WF(p + 0xF0, 1000.0), WF(p + 0xF4, 0.0), WF(p + 0xF8, 0.0)]))
        out.append(case(f'158590 t mode 2 yaw {yaw} flipped', 0x158590, B0, [p, 1, 2],
                        writes=[WF(p + 0xC4, yaw), WF(p + 0xF0, -1000.0), WF(p + 0xF4, 0.0), WF(p + 0xF8, 0.0)]))
    # 001B0300 over a synthetic scene entry (the pointer table entry for
    # area 0x11 / sub-state 0 redirected to it): byte +0x10 with bit 6 set
    # (the low-bits byte keeps seven bits), and word +0x10 with bit 31 set
    # (the row index is an arithmetic shift: row -1)
    ent, tab = SCRATCH + 0x6C000, SCRATCH + 0x6C100
    for word in (0x000001C5, 0xFFFFFFC5, 0x000002C0):
        out.append(case(f'1B0300 t scene entry word10 {word:08x}', 0x1B0300, B0, [],
                        writes=[W8(0x810700, 0x11), W8(0x810701, 0), W8(0x810702, 0), W32(0x24D650 + 0x11 * 4, tab),
                                W32(tab, ent), W32(ent + 0x10, word), W32(ent + 0x18, 0x3F000000),
                                W8(0x8101E7, 0xA5), W8(0x8101E5, 0xA5), W8(0x8101E6, 0xA5)]))
    # 001E3D90: D_00275C00 negative (a signed compare against 0x101), and
    # +0xB8 exactly -700.0 and one ulp either side (the strict float bound)
    r0 = owners(B0, 0x1E3D90)[0]
    for c00 in (0xFFFFFFFF, 0x80000000, 0x101, 0x100):
        out.append(case(f'1E3D90 t 5C00 {c00:08x}', 0x1E3D90, B0, [r0],
                        writes=[W8(r0 + 4, 1), W8(r0 + 0xD, 0), W8(0x8101E4, 0), W8(0x810700, 1), W8(0x810701, 0),
                                W8(0x810702, 0), W32(0x275C00, c00)],
                        script={0x1CD070: [0x3001], 0x1CD2B0: [(0, FB(2.0))]}))
    for yb in (0xC42F0000, 0xC42F0001, 0xC42EFFFF):
        out.append(case(f'1E3D90 t key100 y bits {yb:08x}', 0x1E3D90, B0, [r0],
                        writes=[W8(r0 + 4, 1), W8(0x8101E4, 3), W8(0x810700, 1), W8(0x810701, 0), W32(r0 + 0xB8, yb)],
                        script={0x1CD070: [0x3001], 0x1CD2B0: [(0, FB(2.0))]}))
    # 001287F0: the stored halfword +0xF8 is compared signed with (short)a2
    # (a negative value equal to a2 returns early)
    ac = owners(B0, 0x128C10)[1]
    for cur, clip in ((-3, -3), (-3, 0xFFFD), (0x7FFF, 0x7FFF), (-0x8000, 0x8000)):
        out.append(case(f'1287F0 t signed F8 {cur}/{clip:x}', 0x1287F0, B0, [ac, ac + 0x1F0, clip & MASK],
                        writes=[W16(ac + 0x1F0 + 0xF8, cur)], fargs=[FB(4.0)]))
    # 00128B80: +0x36 is a halfword (a value with only its high byte set
    # still counts as set)
    for s36 in (0x100, 0x8000, 0x0001):
        out.append(case(f'128B80 t halfword 36 {s36:x}', 0x128B80, B0, [ac, ac + 0x1F0],
                        writes=[W16(ac + 0x36, s36), W16(ac + 0x34, 0x1234), W8(0x81080F, 0),
                                W8(ac + 5, 0xA5), W8(ac + 6, 0x5A), W8(ac + 7, 0xC3)]))
    # 00157CE0 type 0x38: the halfword +0x34 is signed; D_00810CB2 between
    # the signed and the unsigned reading of +0x34 * 2
    a = owners(B0, 0x128C10)[0]
    for s34, cb2 in ((-2, 0), (-2, -4), (-2, -3), (-0x4000, 0x7FFF), (0x7FFF, -2)):
        out.append(case(f'157CE0 t signed 34 {s34}/{cb2}', 0x157CE0, B0, [a, 2],
                        writes=[W8(a + 0xB, 4), W8(a + 3, 0x38), W8(0x810C7F, 1), W16(a + 0x34, s34),
                                W16(0x810CB2, cb2), WF(a + 0xB4, 0.0), WF(0x810354, 0.0)]))
    return out


def cases_edges4():
    """Round-4 edges (docs/AREA01_SYS.md section 4, the systematic sweep):
    each makes one original operation observable that no other case
    separates from its single-operation neighbours."""
    out = []
    B0 = BEATS[0]
    ac = owners(B0, 0x128C10)[1]
    # 00128B80: D_0081080F is a byte (the byte after it set, D_0081080F
    # clear, +0x36 clear: result 0), and the bytes after the ones it stores
    # (+8, +0x38..+0x39) poisoned so a wider store shows
    for g, nxt in ((0, 0x5A), (0, 0x80), (1, 0xA5)):
        for s36 in (0, 0x100):
            out.append(case(f'128B80 t byte 80F {g}/{nxt:x} 36 {s36:x}', 0x128B80, B0, [ac, ac + 0x1F0],
                            writes=[W16(ac + 0x36, s36), W16(ac + 0x34, 0x8123), W8(0x81080F, g),
                                    W8(0x810810, nxt), W8(ac + 5, 0xA5), W8(ac + 6, 0x5A), W8(ac + 7, 0xC3),
                                    W8(ac + 8, 0x3C), W16(ac + 0x38, 0x5AA5), W8(ac, 0x66), W8(ac + 4, 0x77)]))
    # 00157CE0: byte +0x0B tested bit by bit (bit 2 gates the routine, bit 0
    # picks the 'result 3' path): every single bit, every bit but bit 2, and
    # bit 2 with bit 1 but not bit 0, on both type paths
    a = owners(B0, 0x128C10)[0]
    for bb in (1, 2, 8, 0x10, 0x20, 0x40, 0x80, 0xFB, 6, 0xFE, 0x0C, 0x84):
        for typ in (0x38, 0x12):
            out.append(case(f'157CE0 t byte0B {bb:x} t{typ:x}', 0x157CE0, B0, [a, 2],
                            writes=[W8(a + 0xB, bb), W8(a + 3, typ), W8(0x810C7F, 0), W8(0x810C63, 2),
                                    W16(0x810CB4, 61), W8(a, 0x5A), W8(a + 0xA, 0x5A)]))
    # 00157CE0 type 0x38 past D_00810C7F: the halfwords D_00810CB2 and +0x34
    # with only their high byte set (a byte reading differs), and the float
    # test +0xB4 < 6.0 + D_00810354 at its edges: +0xB4 one ulp below 6.0,
    # D_00810354 = 1.0 (a halfword reading of it is 0), and D_00810354 =
    # 0.75 ulp(6.0), where the EE sum truncates to 6.0 and a rounded sum
    # would be one ulp above
    for name, cb2, s34, b4, d354 in (('cb2 hi', 0x100, 1, 0.0, 0.0), ('34 hi', 5, 0x100, 0.0, 0.0),
                                     ('b4 below 6', 0x7FFF, 1, 0x40BFFFFF, 0), ('354 one', 0x7FFF, 1, FB(6.5), FB(1.0)),
                                     ('354 frac', 0x7FFF, 1, FB(6.0), FB(0.75 * 2.0 ** -21)),
                                     ('354 frac b4 hi', 0x7FFF, 1, 0x40C00001, FB(0.75 * 2.0 ** -21))):
        out.append(case(f'157CE0 t {name}', 0x157CE0, B0, [a, 2],
                        writes=[W8(a + 0xB, 4), W8(a + 3, 0x38), W8(0x810C7F, 1), W16(a + 0x34, s34),
                                W16(0x810CB2, cb2), W32(a + 0xB4, b4 if isinstance(b4, int) else FB(b4)),
                                W32(0x810354, d354 if isinstance(d354, int) else FB(d354)),
                                W32(0x247274, 0x5A5A5A5A)]))
    # 00158590 mode -1 with scratch words and a position whose sums are
    # inexact (so the EE truncation shows), and mode 2 at the origin (the
    # scaled cosine / sine land in the sums unchanged)
    p = owners(B0, 0x159B90)[0]
    rs = random.Random(0x158)
    for k in range(4):
        pos = [rs.uniform(-900.0, 900.0) for _ in range(3)]
        sc = [rs.uniform(-3.0, 3.0) for _ in range(3)]
        out.append(case(f'158590 t mode -1 inexact {k}', 0x158590, B0, [p, k & 1, MASK],
                        writes=[WF(p + 0xB0, pos[0]), WF(p + 0xB4, pos[1]), WF(p + 0xB8, pos[2]),
                                WF(0x700038B0, sc[0]), WF(0x700038B4, sc[1]), WF(0x700038B8, sc[2]),
                                WF(p + 0xF0, 0.0), WF(p + 0xF4, 0.0), WF(p + 0xF8, 0.0)]))
    for yaw in (0.37, 1.3, 2.9, -2.2):
        out.append(case(f'158590 t mode 2 origin yaw {yaw}', 0x158590, B0, [p, 0, 2],
                        writes=[WF(p + 0xC4, yaw), WF(p + 0xB0, 0.0), WF(p + 0xB4, 0.0), WF(p + 0xB8, 0.0),
                                WF(p + 0xF0, 0.0), WF(p + 0xF4, 0.0), WF(p + 0xF8, 0.0),
                                W32(0x700038B4, 0x3F812345)]))
    # 001A8840 at the edges of its three box tests, with a private extent
    # block: equal to the extent (a strict test would fail), and a
    # difference 0.75 ulp above the extent (the EE subtraction truncates it
    # onto the extent; a rounded one would not); the height test against
    # 1.5 + extent[1] at 1.5 and one ulp above, and with extent[1] = 0.75
    # ulp(1.5) (the EE sum truncates to 1.5)
    import math
    a1 = owners(B0, 0x128C10)[1]
    pl = 0x8102B0
    ext = SCRATCH + 0x300
    u1 = 2.0 ** -23                        # ulp(1.0) = ulp(1.5)
    for name, ax, ay, az, bx, by, bz, e1 in (
            ('x eq', 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
            ('x sub', 1.0, 0.0, 0.0, -0.75 * u1, 0.0, 0.0, 0.0),
            ('z eq', 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0),
            ('z sub', 0.0, 0.0, 1.0, 0.0, 0.0, -0.75 * u1, 0.0),
            ('y eq', 0.0, 1.5, 0.0, 0.0, 0.0, 0.0, 0.0),
            ('y ulp', 0.0, 1.5 + u1, 0.0, 0.0, 0.0, 0.0, 0.0),
            ('y add', 0.0, 1.5 + u1, 0.0, 0.0, 0.0, 0.0, 0.75 * u1),
            ('y sub', 0.0, 1.5, 0.0, 0.0, -0.75 * u1, 0.0, 0.0)):
        out.append(case(f'1A8840 t edge {name}', 0x1A8840, B0, [pl, a1],
                        writes=[WF(pl + 0xA0, ax), WF(pl + 0xA4, ay), WF(pl + 0xA8, az), WF(a1 + 0xB0, bx),
                                WF(a1 + 0xB4, by), WF(a1 + 0xB8, bz), W32(a1 + 0x30, ext), WF(ext, 1.0),
                                WF(ext + 4, e1), WF(ext + 8, 1.0), W8(a1 + 0xD, 0), W8(a1 + 0xB, 1),
                                W8(0x810707, 0), W8(pl, 1), W8(a1 + 0xA, 0x5A)]))
    # 001A9E00 / 001AA000 contacts between records near the origin with
    # radii and positions that are not round: the subtractions, sums and
    # products are inexact, so the EE truncation shows in the positions
    # they store; and an exact contact (3-4-5 triangle) at d == sum
    ra, rb = owners(B0, 0x128C10)[4], owners(B0, 0x128C10)[5]
    ea, eb = SCRATCH + 0x340, SCRATCH + 0x360
    rj = random.Random(0x1A9E)
    for k in range(10):
        r0, r1 = rj.uniform(0.3, 1.7), rj.uniform(0.3, 1.7)
        ang = rj.uniform(-3.1, 3.1)
        dist = rj.uniform(0.2, 0.95) * (r0 + r1)
        bx, bz, by = rj.uniform(-2.0, 2.0), rj.uniform(-2.0, 2.0), rj.uniform(-1.0, 1.0)
        ax, az = bx + dist * math.cos(ang), bz + dist * math.sin(ang)
        h0, h1 = rj.uniform(0.3, 2.0), rj.uniform(0.3, 2.0)
        ay = by + rj.uniform(-0.9, 0.9) * (h0 + h1) / 2
        common = [W32(ra + 0x30, ea), W32(rb + 0x30, eb), WF(ea, r0), WF(eb, r1), WF(ea + 4, h0), WF(eb + 4, h1),
                  W8(rb + 3, 0), W8(rb + 0xB, 0), W8(ra, rj.choice((0, 1, 4)))]
        out.append(case(f'1A9E00 t inexact contact {k}', 0x1A9E00, B0, [ra, rb],
                        writes=common + [WF(ra + 0xA0, ax), WF(ra + 0xA4, ay), WF(ra + 0xA8, az),
                                         WF(rb + 0xB0, bx), WF(rb + 0xB4, by), WF(rb + 0xB8, bz)]))
        out.append(case(f'1AA000 t inexact contact {k}', 0x1AA000, B0, [ra, rb, SCRATCH, SCRATCH + 0x100],
                        writes=common + [WF(ra + 0xB0, ax), WF(ra + 0xB4, ay), WF(ra + 0xB8, az),
                                         WF(rb + 0xB0, bx), WF(rb + 0xB4, by), WF(rb + 0xB8, bz),
                                         W32(SCRATCH + 0xE4, 0x100), W32(SCRATCH + 0x1E4, 0x100)]))
    for fn in (0x1A9E00, 0x1AA000):
        w = [W32(ra + 0x30, ea), W32(rb + 0x30, eb), WF(ea, 2.0), WF(eb, 3.0), WF(ea + 4, 1.0), WF(eb + 4, 1.0),
             W8(rb + 3, 0), W8(rb + 0xB, 0), W8(ra, 0), W32(SCRATCH + 0xE4, 0x100), W32(SCRATCH + 0x1E4, 0x100)]
        pos = 0xA0 if fn == 0x1A9E00 else 0xB0
        w += [WF(ra + pos, 3.0), WF(ra + pos + 4, 0.0), WF(ra + pos + 8, 4.0), WF(rb + 0xB0, 0.0),
              WF(rb + 0xB4, 0.0), WF(rb + 0xB8, 0.0)]
        args = [ra, rb] if fn == 0x1A9E00 else [ra, rb, SCRATCH, SCRATCH + 0x100]
        out.append(case(f'{fn:06X} t contact 3-4-5', fn, B0, args, writes=w))
    # 0015A2C0: state 0 with a row whose words overflow when doubled (the EE
    # product saturates; an IEEE one would be infinite); state 1 with the
    # timer +0x20 at the float edges of its tests (99.99999 + 1 below 100,
    # 119.00001 + 1 above 120, 1.0 - 1 = 0) and infinite (the EE divide
    # saturates); the frame word 0x70003B64 with one low bit set at a time
    q = owners(B0, 0x15A2C0)[0]
    out.append(case('15A2C0 t st0 row overflow', 0x15A2C0, B0, [q],
                    writes=[W8(q + 4, 0), W16(q + 0x54, 0), W32(0x248120, 0x7F000000), W32(0x248128, 0xFF000001),
                            W16(q + 0x56, 0)], script={0x15A200: [1, 0]}))
    for name, link, sub, xa, t20 in (('100 edge', 1, 0, 1, 0x42C5FFFF), ('120 edge', 2, 0, 1, 0x42EE0001),
                                     ('zero edge', 1, 1, 0, FB(1.0)), ('inf sub1', 1, 1, 1, 0x7F800000),
                                     ('inf sub0', 1, 0, 0, 0xFF800000), ('zero edge link2', 2, 1, 0, FB(1.0))):
        out.append(case(f'15A2C0 t {name}', 0x15A2C0, B0, [q],
                        writes=[W8(q + 4, 1), W16(q + 0x56, link), W8(q + 5, sub), W8(q + 0xA, xa), W32(q + 0x20, t20),
                                W16(q + 0x2E, 0), W32(q + 0x80, 0x5A5A5A5A)], script={0x15A200: [1]}))
    for fw in (1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x10000):
        out.append(case(f'15A2C0 t frame bit {fw:x}', 0x15A2C0, B0, [q],
                        writes=[W8(q + 4, 1), W16(q + 0x56, 1), W8(q + 5, 1), W8(q + 0xA, 1), W32(0x70003B64, fw)]))
    # 00159B90 with a non-identity matrix at +0xD0 (the w the matrix
    # product leaves is not 1.0, so the second 1.0 stored at 0x700038AC
    # shows)
    p = owners(B0, 0x159B90)[0]
    rm = random.Random(0x159)
    out.append(case('159B90 t matrix', 0x159B90, B0, [p],
                    writes=[W8(p + 4, 1), W8(p + 5, 4)] + [WF(p + 0xD0 + 4 * k, rm.uniform(-2.0, 2.0)) for k in range(16)],
                    script={0x1B17A0: [1]}, indirect=[u32(image(B0)[0], p + 0x4C)]))
    # 001A9E00 height test at its float edges (radii 1.0 / 1.0, so each half
    # height is 0.5 and the limit 1.0): a difference 0.75 ulp above the limit
    # (the EE subtraction truncates onto it), a first sum 1.5 ulp above 0.5
    # (the EE sum keeps one ulp, a rounded one two), the difference equal to
    # the limit, a second half height 0.75 ulp above 0 (the EE limit stays
    # 0.5), and an infinite first height with a0 at -MAX (the EE halving
    # saturates, an IEEE one stays infinite)
    for name, h0, h1, ay, by in (('sub edge', 1.0, 1.0, 0.5, -0.75 * 2.0 ** -23),
                                 ('add edge', 1.0, 1.0, 1.5 * 2.0 ** -24, -0.5),
                                 ('limit eq', 1.0, 1.0, 0.5, 0.0),
                                 ('limit add', 1.0, 1.5 * 2.0 ** -24, 2.0 ** -24, 0.0),
                                 ('inf first', 'inf', -2.0e38, -0.5e38, 0.0),
                                 ('inf second', -2.0e38, 'inf', 'max', -0.6e38)):
        wh = [W32(ra + 0x30, ea), W32(rb + 0x30, eb), WF(ea, 2.0), WF(eb, 3.0),
              W32(ea + 4, 0x7F800000) if h0 == 'inf' else WF(ea + 4, h0),
              W32(eb + 4, 0x7F800000) if h1 == 'inf' else WF(eb + 4, h1),
              W8(rb + 3, 0), W8(rb + 0xB, 0), W8(ra, 0), WF(ra + 0xA0, 1.0), WF(ra + 0xA8, 1.0),
              W32(ra + 0xA4, 0x7F7FFFFF) if ay == 'max' else WF(ra + 0xA4, ay),
              WF(rb + 0xB0, 0.0), WF(rb + 0xB4, by), WF(rb + 0xB8, 0.0)]
        out.append(case(f'1A9E00 t height {name}', 0x1A9E00, B0, [ra, rb], writes=wh))
    # 0015A2C0 link 2: the halfword +0x2E at 0xFF (its increment carries into
    # the high byte)
    q = owners(B0, 0x15A2C0)[0]
    out.append(case('15A2C0 t 2e carry', 0x15A2C0, B0, [q],
                    writes=[W8(q + 4, 1), W16(q + 0x56, 2), W8(q + 5, 0), W8(q + 0xA, 1), WF(q + 0x20, 120.5),
                            W16(q + 0x2E, 0xFF)], script={0x15A200: [1]}))
    # byte fields read with their next byte set (a halfword reading differs):
    # 001A8840's D_00810707 and a0's byte +0; 001A9E00's a0 byte +0 = 8 (bit
    # 3 without bit 2); 001AA000's keys equal in the low halfword only;
    # 001E7CB0's D_00810700 = 0x13 with D_00810701 set
    for name, w in (('707 next', [W8(0x810707, 1), W8(0x810708, 0x5A), W8(pl, 1)]),
                    ('a0 next', [W8(0x810707, 0), W8(pl, 1), W8(pl + 1, 0x5A)])):
        out.append(case(f'1A8840 t {name}', 0x1A8840, B0, [pl, a1],
                        writes=[WF(pl + 0xA0, 0.0), WF(pl + 0xA4, 0.0), WF(pl + 0xA8, 0.0), WF(a1 + 0xB0, 0.0),
                                WF(a1 + 0xB4, 0.0), WF(a1 + 0xB8, 0.0), W32(a1 + 0x30, ext), WF(ext, 1.0),
                                WF(ext + 4, 1.0), WF(ext + 8, 1.0), W8(a1 + 0xD, 0), W8(a1 + 0xB, 1)] + w))
    w = [W32(ra + 0x30, ea), W32(rb + 0x30, eb), WF(ea, 2.0), WF(eb, 3.0), WF(ea + 4, 1.0), WF(eb + 4, 1.0),
         W8(rb + 3, 0), W8(rb + 0xB, 0), WF(ra + 0xA0, 1.0), WF(ra + 0xA4, 0.0), WF(ra + 0xA8, 1.0),
         WF(rb + 0xB0, 0.0), WF(rb + 0xB4, 0.0), WF(rb + 0xB8, 0.0)]
    for b0 in (8, 0x88, 0xFB):
        out.append(case(f'1A9E00 t a0 byte {b0:x}', 0x1A9E00, B0, [ra, rb], writes=w + [W8(ra, b0)]))
    for k2, k3 in ((0x10100, 0x100), (0x100, 0x10100), (0x10200, 0x200), (0x200, 0x10200)):
        out.append(case(f'1AA000 t keys {k2:x}/{k3:x}', 0x1AA000, B0, [ra, rb, SCRATCH, SCRATCH + 0x100],
                        writes=w + [WF(ra + 0xB0, 1.0), WF(ra + 0xB8, 1.0), W32(SCRATCH + 0xE4, k2),
                                    W32(SCRATCH + 0x1E4, k3), W16(0x70003B86, 0x5A5A)]))
    for sub in (4, 5, 6, 7, 8, 9):
        out.append(case(f'1E7CB0 t 701 set {sub}', 0x1E7CB0, B0, [],
                        writes=[W8(0x810700, 0x13), W8(0x810701, 1), W8(0x810702, sub)]))
    # 001A06A0 over a synthetic list whose shape headers carry every low bit
    # (the kind is the top nibble, bit 11 picks the size), each shape kind
    # followed by one the walker calls, so a wrong size shows at the next
    # call
    S3 = SCRATCH + 0x74000
    L, T, R = S3, S3 + 0x2000, S3 + 0x100
    ws = [WF(0x70003190, 0.0), WF(0x70003194, 12.0), WF(0x70003198, 0.0), WF(0x700031A0, 10.0),
          WF(0x700031A4, 2.0), WF(0x700031A8, 10.0), WF(0x700031B0, 5.0), WF(0x700031B4, 6.0),
          WF(0x700031B8, 4.0), W32(0x70003250, T), W16(0x7000324C, 6), W32(0x275B7C, L), W16(0x275B84, 1),
          W32(0x70003254, 0), W32(T + 4 + 3 * 4, 0x100)]
    base = T + 0x100
    ws += [WF(base + 4 * k, v) for k, v in enumerate((-1.0, -1.0, -1.0, 20.0, 20.0, 20.0))]
    shapes = [(0x17FF, 1, 0x14 + 0x18), (0x1FFF, 1, 0x24 + 0x30), (0x27FF, 0, 0x1C), (0x47FF, 0, 0x18),
              (0x4FFF, 0, 0x2C), (0x87FF, 0, 0x14), (0x8FFF, 0, 0x24), (0x2000, 0, 0x1C)]
    ws.append(W16(base + 0x18, len(shapes)))
    at = base + 0x1C
    for kind, n, size in shapes:
        ws += [W16(at, kind), W8(at + 2, n)]
        at += size
    ws += [W32(L, R), W8(R, 1), W8(R + 2, 4), W16(R + 0xE, 3 << 8), W8(R + 0x54, 0x21), W16(0x700030CA, 0xA55A)]
    for hit in (0, 1):
        out.append(case(f'1A06A0 s synthetic noise hit{hit}', 0x1A06A0, B0, [], writes=ws,
                        forced=(0x1A4030, 0x1A50A0, 0x1A5C30),
                        script={0x1A4030: [0, 0], 0x1A50A0: [0, hit], 0x1A5C30: [0, 0, 0, 0]}))
    # 0019B4C0 with every flag bit but the ones it tests (and each tested
    # one alone with the others), over a segment down through a record
    a0 = owners(B0, 0x128C10)[0]
    x, y, z = struct.unpack_from('<3f', image(B0)[0], a0 + 0xB0)
    seg = [WF(0x700038C0, x + 0.3), WF(0x700038C4, y - 2.0), WF(0x700038C8, z + 0.2), WF(0x700038CC, 1.0),
           WF(0x700038D0, x), WF(0x700038D4, y + 2.0), WF(0x700038D8, z), WF(0x700038DC, 1.0)]
    for flags in (0x7FFFFFF9, 0x7FFFFFFB, 0x7FFFFFFD, 0xFFFFFFF9, 0xFFFFFFFB, 0xFFFFFFFD, 0x7FFFFFFF):
        out.append(case(f'19B4C0 t flags {flags:08x}', 0x19B4C0, B0, [a0, 0x700038C0, 0x700038D0, flags],
                        writes=seg))
        out[-1]['full_only'] = flags != 0x7FFFFFF9
    # 001E3D90 state 1: D_008101E4 = 4 (not 3) with the area key 0x100 and
    # +0xB8 below -700; D_008101E4 = 3 with the byte after it set; the key
    # (0, 2) with D_00810702 = 5 (not an early out); and a matrix with
    # non-round translation words (the three EE additions to them truncate)
    r0 = owners(B0, 0x1E3D90)[0]
    sc = {0x1CD070: [0x3001], 0x1CD2B0: [(0, FB(2.0))]}
    for name, w in (('1E4 is 4', [W8(0x8101E4, 4), W8(0x810700, 1), W8(0x810701, 0), WF(r0 + 0xB8, -800.0)]),
                    ('1E5 set', [W8(0x8101E4, 3), W8(0x8101E5, 1), W8(0x810700, 1), W8(0x810701, 0),
                                 WF(r0 + 0xB8, -800.0)]),
                    ('key 0/2 sub 5', [W8(0x8101E4, 0), W8(0x810700, 0), W8(0x810701, 2), W8(0x810702, 5)])):
        out.append(case(f'1E3D90 t {name}', 0x1E3D90, B0, [r0],
                        writes=[W8(r0 + 4, 1), W8(r0 + 0xD, 1)] + w, script=sc))
    # the word after the three phases above 2.0 (the original wraps exactly
    # three phases)
    out.append(case('1E3D90 t fourth word', 0x1E3D90, B0, [r0],
                    writes=[W8(r0 + 4, 1), W8(r0 + 0xD, 1), W8(0x810700, 1), W8(0x810701, 0),
                            WF(r0 + 0x1F0 + 0x1C, 5.0)], script=sc))
    rq = random.Random(0x1E3D)
    for d in (0, 1, 2):
        out.append(case(f'1E3D90 t inexact matrix d{d}', 0x1E3D90, B0, [r0],
                        writes=[W8(r0 + 4, 1), W8(r0 + 0xD, d), W8(0x810700, 1), W8(0x810701, 0)] +
                        [WF(r0 + 0xD0 + 4 * k, rq.uniform(-300.0, 300.0)) for k in range(16)], script=sc))
    # 00158590 with a1 negative (the original tests 'a1 == 0')
    p = owners(B0, 0x159B90)[0]
    out.append(case('158590 t a1 negative', 0x158590, B0, [p, MASK, 1],
                    writes=[WF(p + 0xF0, 0.0), WF(p + 0xF4, 0.0), WF(p + 0xF8, 0.0)]))
    # 00158590: modes below -2 and above 2 (the entry takes any word; the
    # original's first test is 'mode != -2', its later ones are equalities)
    p = owners(B0, 0x159B90)[0]
    for mode in (-3, -0x80000000, 3, 0x7FFFFFFF, -0x7FFFFFFF):
        for a1, f0 in ((0, 1000.0), (1, -1000.0)):
            out.append(case(f'158590 t mode {mode} a{a1} f{f0}', 0x158590, B0, [p, a1, mode & MASK],
                            writes=[WF(p + 0xC4, 0.37), WF(p + 0xF0, f0), WF(p + 0xF4, 0.0), WF(p + 0xF8, 0.0),
                                    WF(p + 0xBC, 3.5)]))
    # 001E3D90 state 1 with the three phases at 0 (the steps then land
    # exactly in the phase words: a step one ulp off shows), just below the
    # 2.0 wrap and at it, for each variant
    r0 = owners(B0, 0x1E3D90)[0]
    for d in (0, 1, 2):
        for ph in (0.0, 1.9999, 2.0):
            out.append(case(f'1E3D90 t phases {ph} d{d}', 0x1E3D90, B0, [r0],
                            writes=[W8(r0 + 4, 1), W8(r0 + 0xD, d), W8(0x810700, 1), W8(0x810701, 0),
                                    WF(r0 + 0x1F0 + 0x10, ph), WF(r0 + 0x1F0 + 0x14, ph),
                                    WF(r0 + 0x1F0 + 0x18, ph)],
                            script={0x1CD070: [0x3001], 0x1CD2B0: [(0, FB(2.0))]}))
    return out


def full_only(cases):
    for c in cases:
        c['full_only'] = True
    return cases


def cases_edges4b():
    """More round-4 edges (docs/AREA01_SYS.md section 4): 001E7D20's level
    fill, grid bounds, relaxation overflow, splash draw and table index;
    001E3D90's texture-offset carries and phase / counter carries; 00128C10
    halfword carries, gates and table reads; 0019CF50 / 001A06A0 / 0019B4C0
    walker edges. Each makes one original operation observable."""
    out = []
    B0 = BEATS[0]
    ram, _ = image(B0)
    w0 = owners(B0, 0x1E7D20)[0]
    blocks = [SCRATCH + 0x10000 + 0x800 * k for k in range(40)]
    grid = u32(ram, 0x275C20)
    # 001E7D20 area 0x13 level fill: the float at +0xB4 on each floor (the
    # floor test is <=), and 16777220.0 on the 1.0-step floor (the EE
    # subtraction truncates, an IEEE one rounds)
    for d, fb in ((0, 0x43040000), (1, 0x43200000), (1, 0x4B800002)):
        out.append(case(f'1E7D20 t level d{d} {fb:08x}', 0x1E7D20, B0, [w0], script={0x1CB5F0: blocks},
                        writes=[W8(w0 + 4, 1), W8(w0 + 0xD, d), W8(0x810700, 0x13), W8(0x810701, 0), W8(w0 + 5, 1),
                                W32(w0 + 0xB4, fb), W8(0x8107F5, 0), W8(0x8107F6, 0), W8(0x810702, 0)]))
    # 001E7D20 state 0: every grid point on the clamp edges (zero extents:
    # x = +0xB0, z = +0xB8), in area 0x13 record 1 and in the areas either
    # side, with D_00810701 set; the record byte after +0x0D set; extents
    # that are not round (the EE products and quotients truncate / round)
    for name, area, sub, x, z in (('z edge', 0x13, 0, 0x44688C00, 0x44664000),
                                  ('z above', 0x13, 0, 0x44688C00, 0x44664001),
                                  ('x above', 0x13, 0, 0x44688001, 0x44660000),
                                  ('x below', 0x13, 0, 0x44687FFF, 0x44660000),
                                  ('area 12', 0x12, 0, 0x44688C00, 0x44660000),
                                  ('area 14', 0x14, 0, 0x44688C00, 0x44660000),
                                  ('701 set', 0x13, 1, 0x44688C00, 0x44660000)):
        out.append(case(f'1E7D20 t init {name}', 0x1E7D20, B0, [w0],
                        writes=[W8(w0 + 4, 0), W8(w0 + 0xD, 1), W8(0x810700, area), W8(0x810701, sub),
                                W32(w0 + 0xB0, x), W32(w0 + 0xB8, z), WF(w0 + 0xC0, 0.0), WF(w0 + 0xC8, 0.0),
                                WF(w0 + 0xB4, 140.0), W8(0x8107F5, 0), W8(0x8107F6, 0)]))
    out.append(case('1E7D20 t init 0E set', 0x1E7D20, B0, [w0],
                    writes=[W8(w0 + 4, 0), W8(w0 + 0xD, 1), W8(w0 + 0xE, 1), W8(0x810700, 0x13), W8(0x810701, 0),
                            W8(0x8107F5, 0xFF), W8(0x8107F6, 0xFF), WF(w0 + 0xB4, 140.0)]))
    rg = random.Random(0x1E7D)
    for k in range(2):
        out.append(case(f'1E7D20 t init inexact {k}', 0x1E7D20, B0, [w0],
                        writes=[W8(w0 + 4, 0), W8(w0 + 0xD, 0), W8(0x810700, 1), W8(0x810701, 0),
                                WF(w0 + 0xB0, rg.uniform(800.0, 950.0)), WF(w0 + 0xB8, rg.uniform(800.0, 950.0)),
                                WF(w0 + 0xC0, rg.uniform(50.0, 150.0)), WF(w0 + 0xC8, rg.uniform(50.0, 150.0)),
                                WF(w0 + 0xB4, 140.0)]))
    # 001E7D20 state 1: a coefficient word +0x28 that is not round (4 + it
    # truncates); heights and that word so large that the accumulator's
    # product overflows (the EE multiply-subtract takes the infinite product,
    # a separate multiply saturates it first); the splash draw exactly 0
    s0 = grid
    out.append(case('1E7D20 t k4 inexact', 0x1E7D20, B0, [w0], script={0x1CB5F0: blocks},
                    writes=[W8(w0 + 4, 1), W8(w0 + 0xD, 0), W8(0x810700, 1), WF(s0 + 0x28, 0.3141592)]))
    out.append(case('1E7D20 t relax overflow', 0x1E7D20, B0, [w0], script={0x1CB5F0: blocks},
                    writes=[W8(w0 + 4, 1), W8(w0 + 0xD, 0), W8(0x810700, 1), WF(s0 + 0x28, 3.0e38)] +
                    [(s0 + 0x8060, struct.pack('<f', 3.0e38) * 0x400)]))
    for kb in (0x7F800000, 0x7F7FFFFF, 0x00400000):
        out.append(case(f'1E7D20 t splash k {kb:08x}', 0x1E7D20, B0, [w0], script={0x1CB5F0: blocks},
                        writes=[W8(w0 + 4, 1), W8(w0 + 0xD, 0), W8(0x810700, 1), W32(s0 + 0x2C, kb)]))
    out.append(case('1E7D20 t splash draw 0', 0x1E7D20, B0, [w0], script={0x1CB5F0: blocks},
                    writes=[W8(w0 + 4, 1), W8(0x810700, 1), W32(u32(ram, 0x24295C) + 0x58, 0xFC77A683)]))
    # 001E7D20 area 0x13 table index: frame word and D_00275C14 equal only in
    # their low halfword; an index 5 whose table entry has every bit set; and
    # the index outside 0..14 without the clamp (the original would read
    # before or past its 15-entry local table: UNDEFINED)
    base = [W8(w0 + 4, 1), W8(0x810700, 0x13), W8(w0 + 0xD, 2), W8(0x810702, 0)]
    out.append(case('1E7D20 t table frame hi', 0x1E7D20, B0, [w0], script={0x1CB5F0: blocks},
                    writes=base + [W32(0x70003B68, 0x10009), W32(0x275C14, 9), W32(0x275C10, 5)]))
    out.append(case('1E7D20 t table entry ones', 0x1E7D20, B0, [w0], script={0x1CB5F0: blocks},
                    writes=base + [W32(0x70003B68, 9), W32(0x275C14, 9), W32(0x275C10, 5),
                                   (0x2553B0 + 5 * 8, b'\xff' * 8)]))
    for c10 in (-2, -1, 15, 0x10005):
        out.append(case(f'1E7D20 t table 14=9 10={c10}', 0x1E7D20, B0, [w0], script={0x1CB5F0: blocks},
                        writes=base + [W32(0x70003B68, 9), W32(0x275C14, 9), W32(0x275C10, c10 & MASK)],
                        fault=True))
    # 001E3D90: seeds whose texture offsets carry differently for +10 / +11
    # and -11 / +11; a negative 001CD2B0 result; the byte after +0x0D set
    # (state 0 and 1); a variant byte above 0x7F in state 0 (UNDEFINED, with
    # the stores before the refusal compared); the counter s3+4 at 0xFFFF;
    # phases at 16777220.0 (their EE decrement truncates)
    r0 = owners(B0, 0x1E3D90)[0]
    sc = {0x1CD070: [0x3001], 0x1CD2B0: [(0, FB(2.0))]}
    for sd in (0xD9B1CF91, 0x52D31E1B, 0x67F95F8E):
        out.append(case(f'1E3D90 t seed carry {sd:08x}', 0x1E3D90, B0, [r0], script=sc,
                        writes=[W8(r0 + 4, 1), W8(r0 + 0xD, 1), W32(r0 + 0x1F0 + 8, sd), W8(0x810700, 1)]))
    out.append(case('1E3D90 t d negative', 0x1E3D90, B0, [r0], script={0x1CD070: [0x3001], 0x1CD2B0: [(0, FB(-2.0))]},
                    writes=[W8(r0 + 4, 1), W8(r0 + 0xD, 1), W8(0x810700, 1)]))
    for st in (0, 1):
        out.append(case(f'1E3D90 t st{st} 0E set', 0x1E3D90, B0, [r0], script=sc,
                        writes=[W8(r0 + 4, st), W8(r0 + 0xD, 1), W8(r0 + 0xE, 1), W8(0x810700, 1)]))
    out.append(case('1E3D90 t st0 d83', 0x1E3D90, B0, [r0], script=sc, fault=True,
                    writes=[W8(r0 + 4, 0), W8(r0 + 0xD, 0x83), W8(0x810700, 1)]))
    out.append(case('1E3D90 t counter carry', 0x1E3D90, B0, [r0], script=sc,
                    writes=[W8(r0 + 4, 1), W8(r0 + 0xD, 1), W32(r0 + 0x1F0 + 4, 0xFFFF), W8(0x810700, 1)]))
    out.append(case('1E3D90 t phases huge', 0x1E3D90, B0, [r0], script=sc,
                    writes=[W8(r0 + 4, 1), W8(r0 + 0xD, 1), W8(0x810700, 1)] +
                    [WF(r0 + 0x1F0 + 0x10 + 4 * k, 16777220.0) for k in range(3)]))
    # 001E3D90 state 0 with the bytes it clears poisoned; state 0 with the
    # first draw 0x7FFFFFFF (the EE conversion truncates it, a rounded one
    # would give 2^31); a matrix translation word just below 32.0 with its
    # last bit set (each EE translation step crosses 32 and truncates)
    out.append(case('1E3D90 t st0 poisoned', 0x1E3D90, B0, [r0], script=sc,
                    writes=[W8(r0 + 4, 0), W8(r0 + 0xD, 1), W8(r0 + 0xC, 0x5A), W8(r0 + 9, 0xA5), W8(r0, 0x66),
                            W8(0x810700, 1)]))
    out.append(case('1E3D90 t st0 draw 7fffffff', 0x1E3D90, B0, [r0], script=sc,
                    writes=[W8(r0 + 4, 0), W8(r0 + 0xD, 1), W8(0x810700, 1),
                            W32(u32(ram, 0x24295C) + 0x58, 0x8DBDBB1E)]))
    for d in (0, 1, 2):
        out.append(case(f'1E3D90 t translation edge d{d}', 0x1E3D90, B0, [r0], script=sc,
                        writes=[W8(r0 + 4, 1), W8(r0 + 0xD, d), W8(0x810700, 1), W8(0x810701, 0),
                                W32(r0 + 0xD0 + 13 * 4, 0x41FFFFFF)]))
    # 001E7D20 state 1: infinite heights (the multiply-subtract takes an
    # infinite product: -MAX; a saturated product first gives MAX - MAX);
    # area 0x13 record 1 with seeds whose random point differs in its last
    # bit between the EE product and a neighbouring constant or an IEEE one;
    # the clamp of D_00275C10 = 13 (kept) when D_00275C14 is not the frame
    out.append(case('1E7D20 t relax inf', 0x1E7D20, B0, [w0], script={0x1CB5F0: blocks},
                    writes=[W8(w0 + 4, 1), W8(w0 + 0xD, 0), W8(0x810700, 1)] +
                    [(s0 + 0x8060, struct.pack('<I', 0x7F800000) * 0x400)]))
    for sd in (0xA02F34A6, 0x5EB561A4, 0x65AA9C82, 0x98418117):
        out.append(case(f'1E7D20 t 13 d1 seed {sd:08x}', 0x1E7D20, B0, [w0], script={0x1CB5F0: blocks},
                        writes=[W8(w0 + 4, 1), W8(w0 + 0xD, 1), W8(0x810700, 0x13), W8(0x810701, 0), W8(w0 + 5, 0),
                                W8(0x810702, 0), W32(u32(ram, 0x24295C) + 0x58, sd)]))
    out.append(case('1E7D20 t table 14=7 10=13', 0x1E7D20, B0, [w0], script={0x1CB5F0: blocks},
                    writes=base + [W32(0x70003B68, 9), W32(0x275C14, 7), W32(0x275C10, 13)]))
    full_only([c for c in out if c['fn'] == 0x1E7D20])
    out += cases_edges4c()
    return out


def cases_edges4c():
    """Round-4 edges for 00128C10, 0015A2C0 and the walkers (see
    cases_edges4b)."""
    out = []
    B0 = BEATS[0]
    ram, _ = image(B0)
    e = owners(B0, 0x128C10)[0]
    b = e + 0x1F0
    ind = u32(ram, e + 0x4C)
    px, py, pz = struct.unpack_from('<3f', ram, 0x810350)
    c4 = struct.unpack_from('<f', ram, e + 0xC4)[0]

    def c128(name, st, sub, extra=(), script=None, kind=0, dist=500.0, xa=0, e1=1):
        w = [W8(e + 4, st), W8(e + 5, sub), W8(e + 0xD, kind), W8(e + 0xA, xa), W8(e + 1, e1),
             W8(0x70003B8D, 0), W32(0x70003B68, 1), W16(0x70003B8A, 0), W16(e + 0x36, 0), W8(0x81080F, 0),
             WF(e + 0xB0, px + dist), WF(e + 0xB4, py), WF(e + 0xB8, pz), W8(b + 0xFA, 0)] + list(extra)
        sc = {0x1B2140: [1]}
        sc.update(script or {})
        out.append(case('128C10 t ' + name, 0x128C10, B0, [e], script=sc, writes=w, indirect=[ind]))

    # halfword b+0xD0 at 0xFF (its increment carries), and at 0x60 (above
    # the 90-frame bound, not equal to it); the byte after 0x70003B8D set;
    # a kind above 0x7F in state 1; the table D_00242F20 with a distinct
    # entry per kind in sub-state 2; e+0x28 = 0x100 in state 4 sub-state 2;
    # the byte before +6 set when +6 is incremented
    for d0 in (0xFF, 0x60, 0x7FFF):
        c128(f'sub1 far d0 {d0:x}', 1, 1, dist=200.0, e1=0, extra=[W16(b + 0xD0, d0)])
    c128('b8e set', 1, 5, extra=[W8(0x70003B8D, 0), W8(0x70003B8E, 1)])
    # sub-state 0 with the clip halfword b+0xF8 not 1 (so 001C67E0 is
    # called before the kind byte is read again), per kind, with and
    # without the byte after the kind set
    for kind in (0, 1, 3, 5, 6, 8):
        for nxt in (0, 1):
            c128(f'sub0 clip kind {kind} next {nxt}', 1, 0, kind=kind, dist=50.0,
                 extra=[W16(b + 0xF8, 0), W8(e + 0xE, nxt), W32(b + 0xE4, 0x100)])
    # callee results -1 where the original tests != 0 / == 0
    c128('sub8 r-1', 1, 8, script={0x1C2770: [MASK, 0]})
    c128('st0 sub1 r-1', 0, 1, kind=0, script={0x129780: [MASK]}, extra=[W8(e, 0x5A), W16(e + 0x54, 0x5A5A)])
    # kinds 4 and 9 with the byte after +0x0D set
    for kind in (4, 9):
        c128(f'st0 sub1 kind {kind} next', 0, 1, kind=kind, script={0x129780: [1]},
             extra=[W8(e + 0xE, 1), W8(e, 0x5A)])
    c128('kind 84', 1, 0, kind=0x84)
    tbl = [W16(0x242F20 + k * 4, 0x100 + 7 * k) for k in range(10)]
    for kind in (2, 3, 6, 7, 8):
        c128(f'sub2 table kind {kind}', 1, 2, kind=kind, extra=tbl + [WF(b + 0xE8, c4 + 0.5), WF(e + 0xC4, c4)])
    for c28 in (0x100, 0x8000):
        c128(f'st4 sub2 {c28:x}', 4, 2, extra=[W16(e + 0x28, c28)])
    c128('sub3 e6 0 d0 1 before', 1, 3, extra=[W8(e + 6, 0), W16(b + 0xD0, 1), WF(b + 0xE8, 0.5), W8(e, 0x5A),
                                               W8(e - 6, 0x5A)])
    # 0015A2C0 state 0 with the record placed so that its halfword +0x56
    # overlaps the link-1 counter D_008106EC (p + 0x57): the counter
    # increment then changes the halfword's high byte before it is tested
    qa = 0x8106EC - 0x57
    out.append(case('15A2C0 t record over counter', 0x15A2C0, B0, [qa],
                    writes=[W8(qa + 4, 0), W16(qa + 0x54, 0), W16(qa + 0x56, 1), W32(0x70003B68, 0),
                            W8(0x2481B0, 1)], script={0x15A200: [0, 0]}))
    # 0015A2C0 link 1 at its 100 edge with the byte 5 before +5 set
    q = owners(B0, 0x15A2C0)[0]
    out.append(case('15A2C0 t 100 edge before', 0x15A2C0, B0, [q],
                    writes=[W8(q + 4, 1), W16(q + 0x56, 1), W8(q + 5, 0), W8(q + 0xA, 1), WF(q + 0x20, 99.0),
                            W8(q - 5, 0x5A), W16(q + 0x2E, 0), W32(q + 0x80, 0x5A5A5A5A)], script={0x15A200: [1]}))
    # 0019B4C0 with bit 31 and a hit, the word after the delta poisoned (the
    # original adds exactly three words)
    a0 = owners(B0, 0x128C10)[0]
    x, y, z = struct.unpack_from('<3f', ram, a0 + 0xB0)
    seg = [WF(0x700038C0, x + 0.3), WF(0x700038C4, y - 2.0), WF(0x700038C8, z + 0.2), WF(0x700038CC, 1.0),
           WF(0x700038D0, x), WF(0x700038D4, y + 2.0), WF(0x700038D8, z), WF(0x700038DC, 1.0),
           WF(0x700031CC, 5.0)]
    out.append(case('19B4C0 t delta fourth word', 0x19B4C0, B0, [a0, 0x700038C0, 0x700038D0, 0x80000006],
                    writes=seg))
    out[-1]['full_only'] = True
    # 001B0300 over a synthetic scene entry (the pointer table entry of area
    # 0x11 redirected): byte +0x10 with bit 7 clear and each of bits 3..5 set
    # (the enable bit is bit 7 alone); sub-state 1 (the area byte is a byte:
    # the sub byte next to it must not reach the table index); and the
    # player point's y just below 32.0 with its last bit set (the EE add of
    # 15.0 crosses 32 and truncates)
    ent, tab = SCRATCH + 0x6D000, SCRATCH + 0x6D100
    for word, sub, y in ((0x00000148, 0, None), (0x00000150, 0, None), (0x00000160, 0, None),
                         (0x000001C5, 1, None), (0x000001C5, 0, 0x41FFFFFF)):
        w = [W8(0x810700, 0x11), W8(0x810701, sub), W8(0x810702, 0), W32(0x24D650 + 0x11 * 4, tab),
             W32(tab + 4 * sub, ent), W32(ent + 0x10, word), W32(ent + 0x18, 0x3F000000),
             W8(0x8101E7, 0xA5), W8(0x8101E5, 0xA5), W8(0x8101E6, 0xA5)]
        if y is not None:
            w.append(W32(0x810354, y))
        out.append(case(f'1B0300 t scene entry {word:08x} sub {sub} y {y}', 0x1B0300, B0, [], writes=w))
    # 0019CF50: bounds at 0xFF (the span end increment carries)
    C0, N, F = SCRATCH + 0x76000, SCRATCH + 0x77000, SCRATCH + 0x78000
    w = [W16(0x70003240 + 2 * i, 0xFF) for i in range(6)]
    cols = [0, 0x200, 0xF8, 0x200, 0, 0x200]
    for i in range(6):
        w += [W32(0x70003228 + 4 * i, C0 + i * 0x200), W16(C0 + i * 0x200 + 0x1FE, cols[i]),
              W32(0x70003210 + 4 * i, N)]
    w += [W32(0x7000320C, 0x7FFF), W32(0x70003208, F), W32(0x700031D0, 0x1234),
          WF(0x70003190, 1.0), WF(0x700031A0, 2.0), WF(0x70003194, 3.0), WF(0x700031A4, 1.0),
          WF(0x70003198, 1.0), WF(0x700031A8, 1.0), WF(0x700031B0, 7.0), WF(0x700031B4, 8.0), WF(0x700031B8, 9.0)]
    for slot in range(0x100):
        w.append(W16(N + slot * 2, 0))
    a = F
    w += [W16(a + 0xC, 0), W16(a + 0xE, 0x200), W16(a + 0x10, 0), W16(a + 0x12, 0x200), W16(a + 0x14, 0),
          W16(a + 0x16, 0x200), W8(a + 0x1A, 0x10)]
    out.append(case('19CF50 s synthetic bound ff', 0x19CF50, B0, [], writes=w, forced=(0x19F1A0, 0x19ED80),
                    script={0x19ED80: [0] * 0x200}))
    out += walker_edges()
    return out


def walker_edges():
    """001A06A0 over a synthetic list (test data in zeroed RAM) whose
    records and boxes sit on the edges of its tests: a record just past the
    count; first byte 0 with the next byte set; type bytes 6 and 0x0C (the
    five-bit mask); a box offset of 0x10000 (a word, not a halfword); an
    index equal to the box count; index 0 with an overlapping box; boxes
    whose faces touch the segment bounds exactly (each strict / non-strict
    test); +0x54 = 0x4F; a shape count byte of 0x80; and a second record
    whose box lies between the hit point and the old segment end (the
    narrowed bounds exclude it)."""
    out = []
    B0 = BEATS[0]
    S4 = SCRATCH + 0x7A000
    L, T, R = S4, S4 + 0x4000, S4 + 0x200
    seg = [WF(0x70003190, 0.0), WF(0x70003194, 12.0), WF(0x70003198, 0.0), WF(0x700031A0, 10.0),
           WF(0x700031A4, 2.0), WF(0x700031A8, 10.0), WF(0x700031B0, 5.0), WF(0x700031B4, 6.0),
           WF(0x700031B8, 4.0), W32(0x70003250, T), W16(0x7000324C, 8), W32(0x275B7C, L),
           W32(0x70003254, 0), W16(0x700030CA, 0xFFFF)]
    forced = (0x1A4030, 0x1A50A0, 0x1A5C30)

    def box(idx, off, bounds, shapes, w):
        at = T + off
        w.append(W32(T + 4 + idx * 4, off))
        w.extend(WF(at + 4 * k, v) for k, v in enumerate(bounds))
        w.append(W16(at + 0x18, len(shapes)))
        p = at + 0x1C
        for kind, n, size in shapes:
            w.extend([W16(p, kind), W8(p + 2, n)])
            p += size

    def lst(records, w):
        w.append(W16(0x275B84, len(records)))
        for k, (b0, b1, typ, idx, k54) in enumerate(records):
            a = R + k * 0x100
            w.extend([W32(L + 4 * k, a), W8(a, b0), W8(a + 1, b1), W8(a + 2, typ), W16(a + 0xE, idx << 8),
                      W8(a + 0x54, k54)])

    one = [(0x2000, 0, 0x1C)]
    full = (-1.0, -1.0, -1.0, 20.0, 20.0, 20.0)
    variants = {
        'past count': ([(1, 0, 3, 5, 0)], {5: full}, 1, [(1, 0, 4, 3, 0)]),
        'b0 zero b1 set': ([(0, 1, 4, 3, 0)], {3: full}, 0, None),
        'type 6': ([(1, 0, 6, 3, 0)], {3: full}, 0, None),
        'type 0c': ([(1, 0, 0xC, 3, 0)], {3: full}, 0, None),
        'idx equals count': ([(1, 0, 4, 8, 0)], {8: full}, 0, None),
        'idx 0': ([(1, 0, 4, 0, 0)], {0: full}, 0, None),
        '54 is 4f': ([(1, 0, 4, 3, 0x4F)], {3: full}, 0, None),
        'min x on max': ([(1, 0, 4, 3, 0)], {3: (10.0, -1.0, -1.0, 20.0, 20.0, 20.0)}, 0, None),
        'max x on min': ([(1, 0, 4, 3, 0)], {3: (-5.0, -1.0, -1.0, 0.0, 20.0, 20.0)}, 0, None),
        'min z on max': ([(1, 0, 4, 3, 0)], {3: (-1.0, -1.0, 10.0, 20.0, 20.0, 20.0)}, 0, None),
        'max z on min': ([(1, 0, 4, 3, 0)], {3: (-1.0, -1.0, -5.0, 20.0, 20.0, 0.0)}, 0, None),
        'min y on max': ([(1, 0, 4, 3, 0)], {3: (-1.0, 12.0, -1.0, 20.0, 20.0, 20.0)}, 0, None),
        'max y on min': ([(1, 0, 4, 3, 0)], {3: (-1.0, -5.0, -1.0, 20.0, 2.0, 20.0)}, 0, None),
    }
    for name, (records, boxes, extra, after) in variants.items():
        w = list(seg)
        off = 0x200
        for idx, bounds in boxes.items():
            box(idx, off, bounds, one, w)
            off += 0x200
        recs = list(records) + (after or [])
        if name == 'past count':
            box(3, off, full, one, w)
            w.append(W16(0x275B84, 1))
            for k, (b0, b1, typ, idx, k54) in enumerate([(1, 0, 3, 5, 0), (1, 0, 4, 3, 0)]):
                a = R + k * 0x100
                w.extend([W32(L + 4 * k, a), W8(a, b0), W8(a + 1, b1), W8(a + 2, typ), W16(a + 0xE, idx << 8),
                          W8(a + 0x54, k54)])
        else:
            lst(recs, w)
        out.append(case(f'1A06A0 s edge {name}', 0x1A06A0, B0, [], writes=w, forced=forced,
                        script={0x1A50A0: [1], 0x1A5C30: [], 0x1A4030: []}))
    # a box offset of 0x10000 from the table (the offset is a word)
    w = list(seg)
    w += [W32(T + 4 + 3 * 4, 0x10000)] + [WF(T + 0x10000 + 4 * k, v) for k, v in enumerate(full)]
    w += [W16(T + 0x10000 + 0x18, 1), W16(T + 0x10000 + 0x1C, 0x2000)]
    lst([(1, 0, 4, 3, 0)], w)
    out.append(case('1A06A0 s edge offset 10000', 0x1A06A0, B0, [], writes=w, forced=forced,
                    script={0x1A50A0: [1]}))
    # a shape whose count byte is 0x80: the next shape lies 0x14 + 0x80 * 0x18
    # bytes on (a signed byte would put it before)
    w = list(seg)
    at = T + 0x200
    w += [W32(T + 4 + 3 * 4, 0x200)] + [WF(at + 4 * k, v) for k, v in enumerate(full)]
    w += [W16(at + 0x18, 2), W16(at + 0x1C, 0x1000), W8(at + 0x1E, 0x80),
          W16(at + 0x1C + 0x14 + 0x80 * 0x18, 0x2000)]
    lst([(1, 0, 4, 3, 0)], w)
    out.append(case('1A06A0 s edge count 80', 0x1A06A0, B0, [], writes=w, forced=forced,
                    script={0x1A4030: [0], 0x1A50A0: [0]}))
    # a hit on the first record, then a second record whose box lies where
    # the narrowed bound of one axis excludes it: per axis, a segment running
    # down from 12.0 to 0.0 (the hit at 5.0 becomes the minimum; a reading
    # of only the low halfword of 12.0 is 0, and would make it the maximum)
    # and a segment starting at the hit coordinate itself (start == hit:
    # the hit becomes the maximum; a strict test makes it the minimum) or
    # below it (only a strict or an equality test differ there)
    k = {'x': 0, 'y': 1, 'z': 2}
    for axis in ('x', 'y', 'z'):
        for name, start, end, lowbox in (('down', 12.0, 0.0, True), ('from hit', 5.0, 10.0, False),
                                         ('below hit', 2.0, 10.0, False)):
            w = list(seg)
            for i in range(3):
                a0, a1 = (start, end) if i == k[axis] else ((0.0, 10.0) if i != 1 else (12.0, 2.0))
                w += [WF(0x70003190 + 4 * i, a0), WF(0x700031A0 + 4 * i, a1)]
            w += [WF(0x700031B0 + 4 * i, 5.0) for i in range(3)]
            bounds = [-1.0, -1.0, -1.0, 20.0, 20.0, 20.0]
            if lowbox:
                bounds[k[axis]], bounds[3 + k[axis]] = 1.0, 3.0      # between 0 and the hit
            else:
                bounds[k[axis]], bounds[3 + k[axis]] = 7.0, 9.0      # between the hit and the end
            box(3, 0x200, full, one, w)
            box(4, 0x400, tuple(bounds), one, w)
            lst([(1, 0, 4, 3, 0x21), (1, 0, 4, 4, 0x22)], w)
            out.append(case(f'1A06A0 s edge narrowed {axis} {name}', 0x1A06A0, B0, [], writes=w, forced=forced,
                            script={0x1A50A0: [1, 1]}))
    # a record whose box offset is 0, with the table header itself shaped
    # like an overlapping box with one shape (the original skips offset 0)
    w = list(seg)
    w += [WF(T + 0, -1.0), W32(T + 4, FB(-1.0)), W32(T + 8, 0), W32(T + 0xC, FB(20.0)), W32(T + 0x10, FB(20.0)),
          W32(T + 0x14, FB(20.0)), W32(T + 0x18, 1), W32(T + 0x1C, 0x2000)]
    lst([(1, 0, 4, 1, 0)], w)
    out.append(case('1A06A0 s edge offset 0', 0x1A06A0, B0, [], writes=w, forced=forced, script={0x1A50A0: [1]}))
    return out


FX_SITE_VALUES = RM.pick(12, 2)


def site_variants(site_of):
    """Single-load-site variants. For each load instruction the translated
    routines execute after a stubbed call (a 'site'), the field it reads is
    the only one changed, at every stub it follows, so no other change turns
    the path away from it; up to FX_SITE_VALUES fx cases per site, the first
    ones in which that load reads a distinct value (a flip is visible only
    for some values: a byte compared with 1 shows the flip of 1 or 0, not of
    4). Full mode only; the default run runs the pinned ones
    (QUICK_SITE_PINS)."""
    out = []
    for pc in sorted(site_of):
        for c in site_of[pc].values():
            out.append(dict(c, name=f"fx site {pc:06X} {c['name'][3:]}", fx_site=pc))
        if RM.FULL:
            c = next(iter(site_of[pc].values()))
            out.append(dict(c, name=f"fx site high {pc:06X} {c['name'][3:]}", fx_site=pc, fx_high=True))
    return out


STORE_SITE_CASES = RM.pick(2, 1)


def store_variants(stored):
    """Store-site variants. `stored` lists (case, SysEE.stores) for the
    cases that ran, in order. A store instruction of the original (a
    'site') is covered when, in some case, every byte it stores differs
    from the byte that was there before. For every site no case covers, the
    first STORE_SITE_CASES ordinary cases that reach it get a variant in
    which every byte that site stores is set, before the call, to the
    complement of the byte it stores. A translation that drops the store,
    or stores fewer bytes, then leaves the complement in memory, which the
    lockstep compares at the next call. Byte and halfword store sites also
    get a second variant, covered or not, in which the bytes after the
    stored field (as many as it has) are set to 0xA5: a translation that
    stores a wider field writes them (with the zero or sign bits of a small
    value, never 0xA5). Fields whose other values select an UNDEFINED input
    (protected()) are left alone. The round-5 and round-6 cases ('late')
    and the deferred-read fx variants (round 5) do not count for coverage,
    so adding them never removes a variant an earlier round's kill came
    from."""
    covered, cand = set(), {}
    for c, st in stored:
        for pc, targets in st.items():
            if not (c.get('late') or c.get('fx_before')) and \
                    all(all(o != n for o, n in zip(old, new)) for _, old, new in targets.values()):
                covered.add(pc)
            if not (c.get('fx') or c.get('store_site') or c.get('load_site') or c.get('stub_result') or c['fault']):
                cand.setdefault(pc, []).append((c, targets))
    out = []
    for pc in sorted(cand):
        narrow = all(size < 4 for c, targets in cand[pc] for size, _, _ in targets.values())
        if pc in covered and not narrow:
            continue
        for c, targets in cand[pc][:STORE_SITE_CASES]:
            target, after = [], []
            for a, (size, _, new) in sorted(targets.items()):
                at = a if 0x70000000 <= a < 0x70004000 else a & 0x1FFFFFF
                if pc not in covered and not protected(c, a, size):
                    target.append((at, bytes(b ^ 0xFF for b in new)))
                if size < 4 and not protected(c, a + size, size):
                    after.append((at + size, bytes([0xA5]) * size))
            # two variants: the stored bytes' complement, and the bytes after
            # them (kept apart: changing one can turn the path away from the
            # store before it happens)
            for kind, poison in (('', target), (' after', after)):
                if poison:
                    out.append(dict(c, name=f"store site {pc:06X}{kind} {c['name']}",
                                    writes=list(c['writes']) + poison, store_site=pc))
    return out


def norm(a):
    """An original address as prepared() indexes it (scratchpad, or RAM)."""
    return a if 0x70000000 <= a < 0x70004000 else a & 0x1FFFFFF


def pre_bytes(c, a, n):
    """The n bytes at original address a in case c's starting memory."""
    ram, spad = image(c['beat'])
    a = norm(a)
    out = bytearray(spad[a - 0x70000000:a - 0x70000000 + n] if a >= 0x70000000 else ram[a:a + n])
    for w, data in c.get('writes', ()):
        w = norm(w)
        for k in range(n):
            if w <= a + k < w + len(data):
                out[k] = data[a + k - w]
    return bytes(out)


def protected(c, a, n):
    """True when [a, a + n) overlaps a field whose other values select a
    documented UNDEFINED input (docs/AREA01_SYS.md section 2): the FX_RANGE
    words, and 001E3D90's record byte +0x0D (above 2 the original uses
    registers it never set). The store- and load-site variants leave these
    alone."""
    fields = [(r, 4) for r in FX_RANGE]
    if c['fn'] == 0x1E3D90:
        fields.append(((c['args'][0] + 0xD) & MASK, 1))
    return any(a < r + m and r < a + n for r, m in fields)


def is_pointer(v):
    return 0x100000 <= v < 0x2000000 or 0x70000000 <= v < 0x70004000


LOAD_SITE_CASES = RM.pick(3, 1)


def load_variants(loaded):
    """Load-site variants. `loaded` lists (case, SysEE.load_sites) for the
    cases that ran, in order. For every data load instruction of the
    original (a 'site'), the first LOAD_SITE_CASES ordinary cases that
    reach it with distinct loaded values (one in quick mode) get up to two
    variants, each changing the starting memory at
    every address that site loads in that case:
      sign  the top bit of the loaded field flipped (a signed and an
            unsigned reading differ, and so do a narrower and the full
            reading), unless the field holds a pointer or a code address
            (the flip would only move it out of mapped memory) or is in
            FX_RANGE;
      next  for byte loads, the byte after the field XOR 0xA5 (a wider
            reading sees it).
    A translation that reads the field with another width or signedness
    then computes with a different value."""
    cand = {}
    for c, ls in loaded:
        if c.get('fx') or c.get('store_site') or c.get('load_site') or c.get('stub_result') or c['fault']:
            continue
        for pc, targets in ls.items():
            seen = cand.setdefault(pc, {})
            key = tuple(v for _, (_, v) in sorted(targets.items()))
            if key not in seen and len(seen) < LOAD_SITE_CASES:
                seen[key] = (c, targets)
    out = []
    for pc in sorted(cand):
        for c, targets in cand[pc].values():
            sign, nxt = [], []
            for a, (size, _) in sorted(targets.items()):
                if protected(c, a, size + (size == 1)):
                    continue
                cur = pre_bytes(c, a, size)
                v = int.from_bytes(cur, 'little')
                if not (size == 4 and is_pointer(v)):
                    sign.append((a, bytes(cur[:-1]) + bytes([cur[-1] ^ 0x80])))
                if size == 1:
                    nb = pre_bytes(c, a + 1, 1)
                    nxt.append((a + 1, bytes([nb[0] ^ 0xA5])))
            for kind, w in (('sign', sign), ('next', nxt)):
                if w:
                    w = [(norm(a), d) for a, d in w]
                    out.append(dict(c, name=f"load site {pc:06X} {kind} {c['name']}",
                                    writes=list(c['writes']) + w, load_site=pc))
    return out


STUB_RESULT_CASES = RM.pick(4, 1)
STUB_RESULTS = (-1, 2, 0x80000000, 0x7FFFFFFF)


def stub_variants(ran):
    """Stub-result variants. For every routine and every stubbed callee
    it calls, the first STUB_RESULT_CASES ordinary cases of that routine
    that call it along distinct paths (distinct branch-outcome sets) get
    one variant per value
    in STUB_RESULTS, in which every call to that callee returns that value
    (v0; f0 unchanged). A translation that tests a callee's result with a
    neighbouring comparison (> 0 for != 0, <= 0 for == 0, >= 1 for == 1)
    then takes another path. The result a real callee can return is not
    assumed: the translation must follow the original for every v0."""
    cand = {}
    for c, extra in ran:
        if c.get('fx') or c.get('store_site') or c.get('load_site') or c.get('stub_result') or c['fault']:
            continue
        path = extra.get('path')
        for fn in extra.get('stubs', ()):
            seen = cand.setdefault((c['fn'], fn), {})
            if path not in seen and len(seen) < STUB_RESULT_CASES:
                seen[path] = c
    out = []
    for (_, fn) in sorted(cand):
        for c in cand[(_, fn)].values():
            for v in STUB_RESULTS:
                sc = dict(c['script'])
                sc[fn] = [(v & MASK, 0)] * 64
                out.append(dict(c, name=f"stub result {fn:06X}={v & MASK:x} {c['name']}", script=sc,
                                stub_result=fn))
    return out


def prestore_variants(ran):
    """Stub pre-store variants: for every fx case that ran (plain, not a
    single-site one), two variants in which each stub, instead of flipping
    loaded fields, sets every field the original stores after it (and
    before the next stub) to the complement of what it stores ('fx store'),
    or the bytes after each byte / halfword field it stores to 0xA5 ('fx
    store after'). A translation that drops a store, or stores a narrower or
    wider field, after a stubbed call then leaves the stub's bytes where
    the original overwrote them."""
    out = []
    for c, _ in ran:
        if c.get('fx') and not c.get('fx_site') and not c.get('fx_store') and not c.get('fx_before'):
            base = {k: v for k, v in c.items() if k not in ('fx', 'fx_keep')}
            out.append(dict(base, name='fx store ' + c['name'][3:], fx_store='target'))
            out.append(dict(base, name='fx store after ' + c['name'][3:], fx_store='after'))
    return out


ALIAS_RECENT = 4        # store instructions before a load that the aliasing variants pair it with
ALIAS_REACH = 0x1000    # a field belongs to the nearest record base at most this far below it


def mapped_span(a, n):
    return a + n <= 0x2000000 or 0x70000000 <= a and a + n <= 0x70004000


def alias_variants(ran):
    """Aliasing variants (round 6, full mode). A translation that reads a
    field before a store (of the routine, or of a run-policy callee) where
    the original reads it after, or keeps a value read earlier where the
    original reads the field again, differs only when that store writes the
    field: when two records alias, or a record lies on a global the routine
    or a callee writes. SysEE.track records, for every load instruction of
    the translated routines, the ALIAS_RECENT distinct store instructions
    executed last before it (at another address). For each such (load,
    store) pair, the first ordinary case that shows it gets one variant in
    which one record is moved so that the loaded field lies on the stored
    bytes: the load's record when it has a base the case controls, else
    the store's. A record base is a pointer argument, or a pointer word the
    routines or their callees load that held that value in the starting
    memory (the word is rewritten); an address belongs to the nearest base
    at most ALIAS_REACH below it, and an address with none is fixed (a
    global, a table). Pairs whose load and store have the same base, or
    are both fixed, cannot alias and get none. The moved record keeps its
    fields: every byte the case loaded through that base is copied to the
    new place, except where the new place holds a byte the case loads
    through another base or at a fixed address (that byte keeps its value:
    the stored field's old contents). A misaligned access in the variant
    stops the original as the EE's address error: not comparable."""
    done, out = set(), []
    for c, x in ran:
        t = x.get('track')
        if not t or c.get('fx') or is_variant(c) or c['fault']:
            continue
        todo = [k for k in t['pairs'] if k not in done]
        if not todo:
            continue
        bases = {}
        for v, at in t['ptrs'].items():
            if pre_bytes(c, at, 4) == struct.pack('<I', v):
                bases[v] = ('ptr', at)
        for i, v in enumerate(c['args']):
            v &= MASK
            if is_pointer(v) and v >= CODE_END:
                bases[v] = ('arg', i)
        vals = sorted(bases)

        def resolve(a):
            i = bisect.bisect_right(vals, a) - 1
            if i >= 0 and a - vals[i] < ALIAS_REACH:
                return bases[vals[i]], vals[i]
            return None
        loads = t['all_loads']
        owner = {a: resolve(a) for a in loads}
        every = {a0 + j for a0, sz in loads.items() for j in range(sz)}
        mine_of = {}
        for k in todo:
            L, n, S, m = t['pairs'][k]
            bl, bs = owner.get(L) or resolve(L), resolve(S)
            if bl is None and bs is None or bl is not None and bs is not None and bl[0] == bs[0]:
                continue
            (a, na), (b, nb), moved = ((L, n), (S, m), bl) if bl is not None else ((S, m), (L, n), bs)
            a2 = (b & ~(nb - 1)) + (a & (nb - 1)) if na <= nb else b & ~(na - 1)
            delta = a2 - a
            key, base = moved
            if key not in mine_of:
                mine = [(a0, sz) for a0, sz in loads.items() if owner[a0] is not None and owner[a0][0] == key]
                mine_of[key] = (mine, every - {a0 + j for a0, sz in mine for j in range(sz)})
            mine, other = mine_of[key]
            if not all(mapped_span((a0 + delta) & MASK, sz) for a0, sz in mine):
                continue
            w = []
            for a0, sz in mine:
                for j in range(sz):
                    dst = (a0 + j + delta) & MASK
                    if dst not in other and not protected(c, dst, 1):
                        w.append((norm(dst), pre_bytes(c, a0 + j, 1)))
            args = list(c['args'])
            if key[0] == 'arg':
                args[key[1]] = (base + delta) & MASK
            else:
                w.append(W32(norm(key[1]), base + delta))
            out.append(dict(c, name=f"alias {k[0]:06X} {k[1]:06X} {c['name']}", args=args,
                            writes=list(c['writes']) + w, alias=k))
            done.add(k)
    return out


# Words at the compared operand: an exponent-255 pattern (a NaN to the host,
# a finite value beyond MAX to the EE compare), of either sign, and a
# denormal of either sign (the EE compare flushes it to zero).
FLOAT_SPECIALS = (0x7FC00000, 0xFFC00000, 0x80000001, 0x00000001)
FLOAT_SITE_CASES = 2


def float_variants(ran):
    """Float-compare variants (round 6, full mode). For every load
    instruction of the translated routines whose loaded word reaches a
    COP1 compare unchanged (SysEE.cmp_sites), the first FLOAT_SITE_CASES
    ordinary cases that reach it with distinct values get one variant per
    FLOAT_SPECIALS word, written at every address that load reads in the
    case. A translation that compares the raw word with a host float
    compare instead of the EE compare (or treats it as an IEEE value) then
    takes the other branch: for a < b or a <= b with a raw operand, the
    exponent-255 word of one sign or the other decides differently."""
    cand = {}
    for c, x in ran:
        t = x.get('track')
        if not t or c.get('fx') or is_variant(c) or c['fault']:
            continue
        ls = x.get('loads', {})
        for pc in t['cmp']:
            targets = ls.get(pc)
            if not targets:
                continue
            seen = cand.setdefault(pc, {})
            key = tuple(v for _, (_, v) in sorted(targets.items()))
            if key not in seen and len(seen) < FLOAT_SITE_CASES:
                seen[key] = (c, targets)
    out = []
    for pc in sorted(cand):
        for c, targets in cand[pc].values():
            addrs = [a for a, (size, _) in sorted(targets.items()) if size == 4 and not protected(c, a, 4)]
            for val in FLOAT_SPECIALS if addrs else ():
                out.append(dict(c, name=f"float site {pc:06X} {val:08x} {c['name']}",
                                writes=list(c['writes']) + [W32(norm(a), val) for a in addrs], load_site=pc))
    return out


def variants(ran):
    """The store-site, load-site, stub-result and stub pre-store variants
    of the cases that ran (`ran`: (case, run_case extra) in order), and the
    aliasing and float-compare variants."""
    return (store_variants([(c, x.get('stores', {})) for c, x in ran]) +
            load_variants([(c, x.get('loads', {})) for c, x in ran]) + stub_variants(ran) +
            prestore_variants(ran) + alias_variants(ran) + float_variants(ran))


def pinned_variants(ran, have=()):
    """The variants named in QUICK_VARIANT_PINS, each built from its base
    case alone, in both modes (full mode adds the ones its own variant list
    does not build; `have` holds the names it built). A base that
    EM_AREA01_SYS_ONLY filtered out gives no pin."""
    by = {c['name']: (c, x) for c, x in ran}
    out = []
    for name in sorted(QUICK_VARIANT_PINS):
        base = QUICK_VARIANT_PINS[name]
        if name in have or base not in by:
            continue
        got = [v for v in variants([by[base]]) if v['name'] == name]
        assert len(got) == 1, ('variant pin not built', name, len(got))
        out += got
    return out


def fx_cases(cases):
    """The side-effect variant of every non-fault case of the routines in
    FX_FNS, and its deferred-read variant ('fx before': at each stub the
    fields the original loaded before it change). The default run runs
    only the ones in QUICK_PINNED."""
    out = []
    for c in cases:
        if c['fault'] or c['fn'] not in FX_FNS:
            continue
        out.append(dict(c, name='fx ' + c['name'], fx=True))
        out.append(dict(c, name='fx before ' + c['name'], fx=True, fx_before=True))
    return out


def site_pins(cases, have=()):
    """The single-load-site fx variants of QUICK_SITE_PINS, in both modes
    (full mode adds the ones its own site variants do not build)."""
    by = {c['name']: c for c in cases}
    out = []
    for name, pc, high in QUICK_SITE_PINS:
        v = f"fx site {'high ' if high else ''}{pc:06X} {name}"
        if name in by and v not in have:
            # 'late': not counted for store-site coverage (store_variants)
            out.append(dict(by[name], name=v, fx=True, fx_site=pc, fx_high=high, late=True))
    return out


def default_cases(cases):
    """The default run's ordinary cases: every 'core' case (the targeted
    cases of rounds 5 to 7), the pinned killing cases (DEFAULT_CASES,
    QUICK_PINNED), the bases of the variant pins and the fault cases (the
    UNDEFINED contract), plus the first captured record of every routine as
    a smoke check."""
    bases = set(QUICK_VARIANT_PINS.values())
    keep = [c for c in cases if c.get('core') or c['name'] in DEFAULT_CASES or c['name'] in QUICK_PINNED
            or c['name'] in bases or c['fault']]
    names = {c['name'] for c in keep}
    for fn in FUNCS:
        first = next((c for c in cases if c['fn'] == fn and 'as captured' in c['name']), None) \
            or next((c for c in cases if c['fn'] == fn and not c['fault']), None)
        if first is not None and first['name'] not in names:
            keep.append(first)
            names.add(first['name'])
    return [c for c in cases if c['name'] in names]


# The pinned killing cases (docs/AREA01_SYS.md section 4, "What the default
# run runs"). Each is the case, fx case or variant that fails for a named
# translation error of a review; the default run and the full run both run
# every one of them.
#
# fx cases (by name, among fx_cases):
QUICK_PINNED = {
    '19B4C0 a01_00_train_room #9 long 80000004',   # S10 U07 U08: flags bit 31
    'fx 1E3D90 #0 s0 d0 2/0',                     # F35: third area-key read after the stubs
    'fx 1E7D20 #6 s1 13/1 d0',                    # F43: area re-read before the 9-quadword block
    'fx before 1E7D20 t 13 d1 p5 0 y200.0 f0',    # F38: grid base / +0x0D read before the splash stub
    'fx before 1E3D90 a01_00_train_room 7a96e0 as captured',   # F34: seed word read before the stubs
    'fx 1A06A0 s synthetic hit1',                  # N05: +0x54 re-read after the 0x700031D4 store
    'fx before 128C10 t sub3 e6 0 d0 1 bit 0 e1 1',   # N15: +6 re-read for its increment
    'fx before 128C10 t st0 sub1 kind 4',          # N17: +0x0D re-read after 00129780
    'fx 128C10 t sub3 e6 0 d0 5 bit 0 e1 1',       # T12
    'fx 128C10 #42 s0 sub0 k8',                    # T14
    'fx before 128C10 t sub2 e8 +0.0',             # T16
    'fx before 1A06A0 s edge narrowed x from hit', # T46
}
# Store-site, load-site, stub-result, aliasing and float-compare variants,
# each built from its base case alone: variant name -> base case name.
QUICK_VARIANT_PINS = {
    'load site 128EC4 sign 128C10 a01_00_train_room 7a8830 as captured':
        '128C10 a01_00_train_room 7a8830 as captured',                            # N12: b+0xE4 word
    'stub result 1A4030=ffffffff 19B4C0 r5 a1 is the segment end': '19B4C0 r5 a1 is the segment end',   # N34
    'stub result 19ED80=ffffffff 19CF50 s synthetic hit': '19CF50 s synthetic hit',   # N35: != 0, not > 0
    'store site 159E44 after 159B90 s2 sub0 r1 b0': '159B90 s2 sub0 r1 b0',      # N40: +4 stored as a byte
    'load site 1B6D78 sign 1B6D70 op 0': '1B6D70 op 0',                          # N50: the whole opcode word
    'load site 1B76E0 sign 1B76D0': '1B76D0',                                    # N51: a2+0x14 sign-extended
    'float site 157DFC ffc00000 157CE0 b4 t38 1 3/6 y0.0 2/60': '157CE0 b4 t38 1 3/6 y0.0 2/60',   # T04
    'float site 1A8894 7fc00000 1A8840 #0': '1A8840 #0',                          # T22
    'alias 1B03B0 1B039C 1B0300 a01_00_train_room 1': '1B0300 a01_00_train_room 1',   # T26
    'alias 1E8004 1E7F70 1E7D20 t init 13/0 d1 fff': '1E7D20 t init 13/0 d1 fff',   # T30
    'stub result 1B0FD0=ffffffff 159B90 s0 sub0 r0 b0': '159B90 s0 sub0 r0 b0',  # T07
    'stub result 1B2140=80000000 128C10 a01_00_train_room 7a8250 as captured':
        '128C10 a01_00_train_room 7a8250 as captured',                            # T19
    'stub result 1CD070=ffffffff 1E3D90 a01_00_train_room 7a96e0 as captured':
        '1E3D90 a01_00_train_room 7a96e0 as captured',                            # T38
    'store site 159C04 after 159B90 s0 sub0 r0 b0': '159B90 s0 sub0 r0 b0',      # U03
}
# The bases of the aliasing and float-compare pins run with SysEE.track in
# the default run too (those variants are built from what it records).
TRACK_BASES = {b for n, b in QUICK_VARIANT_PINS.items() if n.startswith(('alias ', 'float site '))}
# Single-load-site fx variants: (base case, load pc, top-bit flip).
QUICK_SITE_PINS = (('128C10 t sub3 e6 0 d0 1 bit 0 e1 0', 0x1292B4, False),   # F14: table index re-read
                   ('1A06A0 s synthetic hit0', 0x1A09C0, True),               # F30: shape count re-read
                   ('128C10 t sub1 near heading draw 18', 0x129060, True),    # N13: b+0xE4 radius word
                   ('128C10 t sub0 clip kind 0 next 0', 0x128E84, True),      # N16: +0x0D re-read
                   ('1E3D90 #0 s0 d0 2/0', 0x1E4440, False),                  # T40
                   ('1E3D90 a01_00_train_room 7a96e0 as captured', 0x1E442C, True),   # U11
                   ('15A2C0 t zero edge', 0x15A5B0, False))                   # T09
# Ordinary pinned killing cases besides the 'core' ones and QUICK_PINNED.
DEFAULT_CASES = {
    '1287F0 7a8250 clip 2',                        # N45: the register-count check at the first call
    '158590 t negative zero dot m1', '158590 t negative zero dot n m-1',   # N19 (and the other six)
    '158590 t negative zero dot m-1', '158590 t negative zero dot n m1', '158590 t negative zero dot m0',
    '158590 t negative zero dot n m0', '158590 t negative zero dot m2', '158590 t negative zero dot n m2',
    '19B4C0 a01_07_level_exit #14 zero 80000006',  # T42
    '157CE0 t byte0B 8 t38',                       # round-2 Y04
    '158590 t mode -3 a0 f1000.0',                 # round-2 Y06
    '1E3D90 t phases 0.0 d0',                      # round-2 Y31 / Z10 / Z11
}

# The routines that load memory after a stubbed call (the others make no
# stubbed call, or return right after their last one). 0019CF50 and 001A06A0
# only stub a leaf in their forced synthetic cases.
FX_FNS = {0x128B80, 0x128C10, 0x157CE0, 0x158D30, 0x159B90, 0x15A2C0, 0x1A8840, 0x1E3D90, 0x1E7D20,
          0x19CF50, 0x1A06A0}


def cases_review5(prior):
    """Round-5 cases (docs/AREA01_SYS.md section 4, "Round 5"): inputs on
    which a translation error the round-4 test missed changes the result.
    Records that alias (one record's field is another's, or a routine's own
    store lands on a field it reads again), a record placed in the stack
    below the entry stack pointer where a callee's frame lies, the last
    bytes of the RAM and scratchpad regions, clamped splash cells with a
    rate at the centre, a denormal segment end, a hit followed by a record
    whose box has no shape, and a 9-quadword block placed over the render
    context word."""
    by = {c['name']: c for c in prior}
    out = []
    B0 = BEATS[0]
    ram, _ = image(B0)
    # 0015A2C0 state 0: the record placed so that its halfword +0x56 is the
    # link-1 counter byte D_008106EC itself (the original reads the counter
    # again after storing the halfword)
    qa = 0x8106EC - 0x56
    out.append(case('15A2C0 r5 counter low byte', 0x15A2C0, B0, [qa],
                    writes=[W8(qa + 4, 0), W16(qa + 0x54, 0), W16(qa + 0x56, 1), W32(0x70003B68, 0),
                            W8(0x2481B1, 5)], script={0x15A200: [0, 0]}))
    # 001A9E00 with a1 = a0 - 8: its store to a1+0xB0 is a0+0xA8, which the
    # original reads after that store
    a0 = SCRATCH + 0x40100
    P0, P1 = SCRATCH + 0x43000, SCRATCH + 0x43100
    out.append(case('1A9E00 r5 alias a1 = a0 - 8', 0x1A9E00, B0, [a0, a0 - 8],
                    writes=[WF(a0 + 0xA0, 1.3), WF(a0 + 0xA4, 0.0), WF(a0 + 0xA8, 1.0), WF(a0 + 0xAC, 0.0),
                            WF(a0 + 0xB0, 0.6), W32(a0 + 0x30, P0), W32(a0 + 0x28, P1), WF(P0, 1.0),
                            WF(P0 + 4, 2.0), WF(P1, 1.0), WF(P1 + 4, 2.0)]))
    # 001AA000 with a0 = a1 + 8: its store to a0+0xB0 is a1+0xB8
    b1 = SCRATCH + 0x44100
    K = SCRATCH + 0x46000
    Q0, Q1 = SCRATCH + 0x47000, SCRATCH + 0x47100
    out.append(case('1AA000 r5 alias a0 = a1 + 8', 0x1AA000, B0, [b1 + 8, b1, K, K],
                    writes=[WF(b1 + 0xB0, 0.0), WF(b1 + 0xB4, 0.0), WF(b1 + 0xB8, 0.5), WF(b1 + 0xBC, 0.0),
                            WF(b1 + 0xC0, 0.5), W32(b1 + 0x38, Q0), W32(b1 + 0x30, Q1), WF(Q0, 1.0),
                            WF(Q0 + 4, 1.0), WF(Q1, 1.0), WF(Q1 + 4, 1.0)]))
    # 001A8840 with a1 = 0x6FFFFFE0: +0x0D is unmapped, +0x30 and +0xB0 are
    # scratchpad; no contact, so the original never reads +0x0D
    a8 = SCRATCH + 0x48000
    out.append(case('1A8840 r5 unmapped kind byte, no contact', 0x1A8840, B0, [a8, 0x6FFFFFE0],
                    writes=[WF(a8 + 0xA0, 0.0), WF(0x70000090, 100.0), W32(0x70000010, 0x70000100),
                            WF(0x70000100, 1.0), WF(0x70000104, 1.0), WF(0x70000108, 1.0)]))
    # 0019B4C0 with a1 = 0x700031A0 (its mode path restores 0x700031A0 from
    # the copy it kept, not from a1, which a walker hit overwrote)
    syn = {c['name']: c for c in cases_synthetic(random.Random(0))}
    h1 = syn['1A06A0 s synthetic hit1']
    ra0, ra2 = SCRATCH + 0x50000, SCRATCH + 0x50100
    R = SCRATCH + 0x60000 + 0x100
    out.append(case('19B4C0 r5 a1 is the segment end', 0x19B4C0, B0, [ra0, 0x700031A0, ra2, 2],
                    writes=h1['writes'] + [W8(ra0 + 2, 4), W32(ra0 + 0x14, R + 0x200), WF(ra2, 10.0),
                                           WF(ra2 + 4, -10.0), WF(ra2 + 8, 10.0)],
                    script=h1['script'], forced=h1['forced']))
    # 0019CF50: start y = +0, end y = -denormal (the EE compare flushes the
    # denormal to zero: the two are equal)
    cf = next(c for n, c in by.items() if n.startswith('19CF50 s synthetic'))
    for y1 in (0x80000001, 0x807FFFFF, 0x00000001):
        out.append(dict(cf, name=f'19CF50 r5 denormal end y {y1:08x}',
                        writes=cf['writes'] + [W32(0x70003194, 0), W32(0x700031A4, y1)]))
        out.append(dict(cf, name=f'19CF50 r5 denormal start y {y1:08x}',
                        writes=cf['writes'] + [W32(0x70003194, y1), W32(0x700031A4, 0)]))
    # 001A06A0: a hit record followed by a qualifying record whose box has
    # no shape, or whose first shape is of an unknown kind (the hit flag is
    # reset per record)
    S = SCRATCH + 0x90000
    L, TB, RR = S, S + 0x4000, S + 0x200
    full = (-1.0, -1.0, -1.0, 20.0, 20.0, 20.0)
    for label, second in (('no shape', []), ('unknown kind', [(0x0100, 0, 0x1C)])):
        w = [WF(0x70003190, 0.0), WF(0x70003194, 12.0), WF(0x70003198, 0.0), WF(0x700031A0, 10.0),
             WF(0x700031A4, 2.0), WF(0x700031A8, 10.0), WF(0x700031B0, 5.0), WF(0x700031B4, 6.0),
             WF(0x700031B8, 4.0), W32(0x70003250, TB), W16(0x7000324C, 8), W32(0x275B7C, L),
             W32(0x70003254, 0), W16(0x700030CA, 0xFFFF), W16(0x275B84, 2)]
        for idx, off, shapes in ((3, 0x200, [(0x2000, 0, 0x1C)]), (4, 0x400, second)):
            at = TB + off
            w.append(W32(TB + 4 + idx * 4, off))
            w += [WF(at + 4 * k, v) for k, v in enumerate(full)]
            w.append(W16(at + 0x18, len(shapes)))
            q = at + 0x1C
            for kind, n, size in shapes:
                w += [W16(q, kind), W8(q + 2, n)]
                q += size
        for k, idx in enumerate((3, 4)):
            a = RR + k * 0x100
            w += [W32(L + 4 * k, a), W8(a, 1), W8(a + 2, 4), W16(a + 0xE, idx << 8), W8(a + 0x54, 0x11 * (k + 1))]
        out.append(case(f'1A06A0 r5 hit then {label}', 0x1A06A0, B0, [], writes=w,
                        forced=(0x1A4030, 0x1A50A0, 0x1A5C30), script={0x1A50A0: [1]}))
    # 00158590 mode 2 with the record in the stack below the entry stack
    # pointer: p+0xC4 is a word 0011E2A8's frame stores (the run callees run
    # 0x400 below the caller's stack pointer on both sides), so the original,
    # which loads p+0xC4 again for 0011DE90, passes the value 0011E2A8 left
    callee_sp = (SP - 0x50 - 0x400) & ~15
    for slot, x in ((0x1C, 2.0), (0x1C, 100.0)):
        pp = callee_sp - slot - 0xC4
        c = case(f'158590 r5 record in the callee frame {x}', 0x158590, B0, [pp, 0, 2])
        c['stack'] = [(pp + 0xB0, struct.pack('<4f', 0.0, 0.0, 0.0, 0.0)), (pp + 0xC4, struct.pack('<f', x)),
                      (pp + 0xF0, struct.pack('<4f', 0.0, 0.0, 0.0, 0.0))]
        out.append(c)
    # the last word of RAM and of the scratchpad (region end bounds)
    for at in (0x01FFFF48, 0x70003F48):
        out.append(case(f'1B0D80 r5 region end {at:08x}', 0x1B0D80, B0, [at], writes=[WF(at + 0xB4, -300.0)]))
        out.append(case(f'1B0D80 r5 region end {at:08x} not below', 0x1B0D80, B0, [at],
                        writes=[WF(at + 0xB4, -100.0)]))
    # 001E7D20: a splash at a clamped cell (row or column 0 or 31, where a
    # half-rate neighbour is the centre cell itself) with a rate already at
    # the centre (EE additions do not reassociate)
    for name in ('1E7D20 t splash 0,0', '1E7D20 t splash 31,31'):
        base = by.get(name)
        if base is None:
            continue
        p41 = base['args'][0]
        s41 = u32(ram, 0x275C20) + ram[p41 + 0xD] * 0xA060
        row, col = (0, 0) if name.endswith('0,0') else (31, 31)
        cell = s41 + 0x9060 + row * 0x80 + col * 4
        for x in (0x3F800000, 0x32800000, 0x3E99999A):
            v = dict(base, name=f'{name} r5 centre rate {x:08x}', writes=base['writes'] + [W32(cell, x)])
            out.append(v if (row, x) == (0, 0x3F800000) else full_only([v])[0])
    # 001E7D20 packets: the 9-quadword block handed out at 0x275600, so the
    # run-policy copy into blk+0x70 overwrites the render-context word
    # D_00275670 that the original loads again after it
    c42 = by['1E7D20 t 13 d0 p5 0 y200.0 f0']
    blocks = list(c42['script'][0x1CB5F0])
    blocks[31] = 0x275600
    ctx = u32(ram, 0x275670)
    out.append(dict(c42, name='1E7D20 r5 block over the context word', script={**c42['script'], 0x1CB5F0: blocks},
                    writes=c42['writes'] + [W32(ctx + 0x2220, SCRATCH), W32(ctx + 0x2230, 0x12345678)]))
    # 00159B90: the captured record with its sub-state byte +5 XOR 0xA5 (a
    # value above 3: the original's switch takes no case; a translation that
    # masks the byte takes one)
    c6 = by['159B90 a01_00_train_room 7ad490 as captured']
    p6 = c6['args'][0]
    out.append(dict(c6, name='159B90 r5 sub-state byte a5', writes=c6['writes'] + [W8(p6 + 5, ram[p6 + 5] ^ 0xA5)]))
    for c in out:
        c['late'] = True                # not counted for store-site coverage (store_variants)
        if not c.get('full_only'):
            c['core'] = True            # quick mode runs them too
    return out


def cases_review6(prior):
    """Round-6 cases (docs/AREA01_SYS.md section 4, "Round 6"): the inputs
    the round-5 review found for translation errors no earlier case showed.
    Records placed on a field the routine (or a run-policy callee) stores
    before reading it again: 0019B4C0's a0 on the segment words it stores,
    001A9E00's a0 on a1's pushed z, 001B0300's scene entry on the globals it
    clears, 001E7D20's record over the area byte D_00810700, 00158590's
    record over its scratch sums, 0015A2C0's LCG state word (00122BB8, run as
    original) on its row word. Raw words the original hands to an EE
    compare: a negative denormal extent (equal to 0 there), and
    exponent-255 words (0x7FC00000 / 0xFFC00000: beyond +-MAX to the EE
    compare, NaN to a host compare) at segment ends, box bounds, heights
    and the -200 / -700 / floor tests. Both runs run them."""
    by = {c['name']: c for c in prior}
    out = []
    B0 = BEATS[0]
    ram, _ = image(B0)
    A1, A2 = SCRATCH + 0xA0000, SCRATCH + 0xA0100

    def seg(name, a0, flags, extra):
        return case(name, 0x19B4C0, B0, [a0, A1, A2, flags],
                    writes=[WF(A1, 10.0), WF(A1 + 4, 20.0), WF(A1 + 8, 30.0), WF(A2, 1.0), WF(A2 + 4, 2.0),
                            WF(A2 + 8, 3.0)] + extra)
    # 0019B4C0: a0's byte +2 inside the segment start word 0x70003190, which
    # it stores before reading the byte
    out.append(seg('19B4C0 r6 a0 on the segment start', 0x70003190, 0, [W32(0x70003190, 0x12345678)]))
    # a0's word +0x14 is 0x7000324C, whose high halfword it stores first
    out.append(seg('19B4C0 r6 a0+0x14 on 0x7000324C', 0x70003238, 2,
                   [W16(0x7000324E, 0xABCD), W8(0x7000323A, 4), W16(0x275B84, 0)]))
    # bit 31: a0+0xB0 is 0x700031C4, the delta the axis-1 add reads after
    # the axis-0 add stored there
    c = by['19B4C0 r5 a1 is the segment end']
    a0 = 0x70003114
    out.append(dict(c, name='19B4C0 r6 a0+0xB0 on the delta, bit 31',
                    args=[a0, c['args'][1], c['args'][2], 0x80000002],
                    writes=list(c['writes']) + [W8(a0 + 2, 4), W32(a0 + 0x14, SCRATCH + 0x60300)]))
    # 001A9E00: a0 = a1 + 0xB8, so a0's byte +0 is the low byte of a1's z,
    # which the push-out stores before the original reads the byte
    S6 = SCRATCH + 0xA2000
    P0, P1 = SCRATCH + 0xA3000, SCRATCH + 0xA3100
    for z in (0.7, 0.55, 0.9, 0.35):
        a0, a1 = S6 + 0xB8, S6
        out.append(case(f'1A9E00 r6 a0 = a1 + 0xB8 z{z}', 0x1A9E00, B0, [a0, a1],
                        writes=[W32(a1 + 0x30, P1), W8(a1 + 3, 0), WF(a1 + 0xB0, 0.0), WF(a1 + 0xB4, 0.0),
                                WF(a1 + 0xB8, 0.0), W32(a0 + 0x30, P0), WF(a0 + 0xA0, 0.3), WF(a0 + 0xA4, 0.0),
                                WF(a0 + 0xA8, z), WF(P0, 1.0), WF(P0 + 4, 2.0), WF(P1, 1.0), WF(P1 + 4, 2.0)]))

    # 001B0300: the scene entry p placed so that p+0x18 holds D_008101E7
    # (cleared before the read), or p+0x10 is D_008101EC (stored before it)
    def scene(p, extra):
        T0 = SCRATCH + 0x70000
        return [W8(0x810700, 0), W8(0x810701, 0), W8(0x810702, 0), W32(0x24D650, T0), W32(T0, p)] + extra
    out.append(case('1B0300 r6 entry at 0x008101CC', 0x1B0300, B0, [],
                    writes=scene(0x8101CC, [W32(0x8101E4, 0x3F800000), W8(0x8101E7, 0x5A), W8(0x8101DC, 0)])))
    out.append(case('1B0300 r6 entry at 0x008101DC', 0x1B0300, B0, [],
                    writes=scene(0x8101DC, [W32(0x8101F4, 0x3F800005), W32(0x8101EC, 0)])))
    # 001E7D20 state 0: p+0x20 is D_00810700 (cleared: the original then
    # takes the area-0 constants); record 1 with p+0xB4 on D_00810700 (the
    # 160.0 level store clears the area byte before the grid clamp)
    p9 = 0x8106E0
    out.append(case('1E7D20 r6 p+0x20 on the area byte', 0x1E7D20, B0, [p9],
                    writes=[W8(p9 + 4, 0), W8(p9 + 0xD, 0), W32(p9 + 0x20, 0x13)]))
    p10 = 0x81064C
    out.append(case('1E7D20 r6 p+0xB4 on the area byte', 0x1E7D20, B0, [p10],
                    writes=[W8(p10 + 4, 0), W8(p10 + 0xD, 1), W32(p10 + 0xB4, 0x13), W8(0x8107F5, 0xFF),
                            WF(p10 + 0xB0, 1000.0), WF(p10 + 0xB8, 0.0), WF(p10 + 0xC0, 60.0),
                            WF(p10 + 0xC8, 60.0)]))
    # 00158590 mode -1: p+0xB4 is 0x700038A0, the first sum it stores
    p11 = 0x700037EC
    out.append(case('158590 r6 p+0xB4 on the sum 0x700038A0', 0x158590, B0, [p11, 0, MASK],
                    writes=[WF(p11 + 0xB0, 1.0), WF(p11 + 0xB4, 2.0), WF(p11 + 0xB8, 3.0), WF(0x700038B0, 1.0),
                            WF(0x700038B4, 2.0), WF(0x700038B8, 3.0), WF(p11 + 0xF0, 0.0), WF(p11 + 0xF4, 0.0),
                            WF(p11 + 0xF8, 0.0), WF(p11 + 0xFC, 0.0)]))
    # 0015A2C0 state 0 row 0: 00122BB8 keeps its state at *(D_0024295C) +
    # 0x58; that word is the row word 0x00248120, read after the draw
    c = by['15A2C0 r5 counter low byte']
    out.append(dict(c, name='15A2C0 r6 LCG state on the row word',
                    writes=list(c['writes']) + [W32(0x24295C, 0x248120 - 0x58)]))
    # raw words at an EE compare
    a8, a81, X = SCRATCH + 0xB0000, SCRATCH + 0xB0400, SCRATCH + 0xB0800
    out.append(case('1A8840 r6 extent x -denormal', 0x1A8840, B0, [a8, a81],
                    writes=[WF(a8 + 0xA0, 0.0), WF(a8 + 0xA4, 0.0), WF(a8 + 0xA8, 0.0), WF(a81 + 0xB0, 0.0),
                            WF(a81 + 0xB4, 0.0), WF(a81 + 0xB8, 0.0), W32(a81 + 0x30, X), W32(X, 0x80000001),
                            WF(X + 4, 1.0), WF(X + 8, 1.0), W8(a81 + 0xD, 1)]))
    L, TB = SCRATCH + 0x90000, SCRATCH + 0x94000
    E = SCRATCH + 0x90200
    at = TB + 0x200
    hit = ([WF(0x70003190, 0.0), WF(0x70003194, 12.0), WF(0x70003198, 0.0), WF(0x700031A0, 10.0),
            WF(0x700031A4, 2.0), WF(0x700031A8, 10.0), WF(0x700031B0, 5.0), WF(0x700031B4, 6.0),
            WF(0x700031B8, 4.0), W32(0x70003250, TB), W16(0x7000324C, 8), W32(0x275B7C, L),
            W32(0x70003254, 0), W16(0x700030CA, 0xFFFF), W16(0x275B84, 1), W32(TB + 4 + 3 * 4, 0x200)] +
           [WF(at + 4 * k, v) for k, v in enumerate((-1.0, -1.0, -1.0, 20.0, 20.0, 20.0))] +
           [W16(at + 0x18, 1), W16(at + 0x1C, 0x2000), W8(at + 0x1E, 0), W32(L, E), W8(E, 1), W8(E + 2, 4),
            W16(E + 0xE, 3 << 8), W8(E + 0x54, 0x11)])
    lh = dict(forced=(0x1A4030, 0x1A50A0, 0x1A5C30), script={0x1A50A0: [1]})
    out.append(case('1A06A0 r6 end x 7fc00000', 0x1A06A0, B0, [],
                    writes=hit + [WF(0x70003190, 1.0), W32(0x700031A0, 0x7FC00000)], **lh))
    out.append(case('1A06A0 r6 box min x 7fc00000', 0x1A06A0, B0, [], writes=hit + [W32(at, 0x7FC00000)], **lh))
    cf = by['19CF50 s synthetic hit']
    for y in (0x80000001, 0x807FFFFF):
        out.append(dict(cf, name=f'19CF50 r6 end x {y:08x}', writes=cf['writes'] + [W32(0x70003190, 0),
                                                                                   W32(0x700031A0, y)]))
        out.append(dict(cf, name=f'19CF50 r6 end z {y:08x}', writes=cf['writes'] + [W32(0x70003198, 0),
                                                                                   W32(0x700031A8, y)]))
    ab = SCRATCH + 0xB1000
    out.append(case('1B0D80 r6 y ffc00000', 0x1B0D80, B0, [ab], writes=[W32(ab + 0xB4, 0xFFC00000)]))
    c = by['1E3D90 a01_00_train_room 7a96e0 as captured']
    out.append(dict(c, name='1E3D90 r6 key 100 z ffc00000',
                    writes=c['writes'] + [W8(0x8101E4, 3), W8(0x810700, 1), W8(0x810701, 0),
                                          W32(c['args'][0] + 0xB8, 0xFFC00000)]))
    c = by['1E7D20 t 13 d0 p5 1 y200.0 f0']
    out.append(dict(c, name='1E7D20 r6 level ffc00000', writes=c['writes'] + [W32(c['args'][0] + 0xB4, 0xFFC00000)]))
    for c in out:
        c['core'] = True                # quick mode runs them too
        c['late'] = True                # not counted for store-site coverage (store_variants)
        c.pop('full_only', None)
    return out


def cases_review7(prior):
    """Round-7 cases (docs/AREA01_SYS.md section 4, "Round 7"): the inputs
    the round-6 review and the final review found for translation errors
    no earlier case showed. Each is an existing case with one field or
    argument moved to a value the original accepts and no case had used:
    00128C10's approach goal b+0xE8 equal to the current heading only under
    the EE compare (+0 / -0 / a denormal); 00158590's a1 with only its upper
    half set; 001E7D20's 9-quadword block handed out at 0x008106F0, so the
    block's own stores rewrite the area bytes D_00810700 / D_00810701 that
    the original reads again (areas 0x13 and 6); 001A06A0 with an
    exponent-255 segment start (-MAX to the EE compare, NaN to a host
    compare) before a hit and a second record the narrowed bound excludes;
    001AA000's key words 0x201 / 0x2FF (high byte 2, not 0x200); and
    001A9E00's a1+0x0B holding bits other than bit 0 before the store of 1.
    Both runs run them."""
    by = {c['name']: c for c in prior}
    out = []
    c = by['128C10 t sub2 e8 +0.0']
    e = c['args'][0]
    for v, w in ((0x00000001, 0), (0x80000000, 0), (0x80000001, 0x00000001)):
        out.append(dict(c, name=f'128C10 r7 goal {v:08x} heading {w:08x}',
                        writes=list(c['writes']) + [W32(e + 0x1F0 + 0xE8, v), W32(e + 0xC4, w)]))
    c = by['158590 t a1 negative']
    for a1, mode in ((0x00010000, -2), (0x7FFF0000, 1)):
        out.append(dict(c, name=f'158590 r7 a1 {a1:08x} mode {mode}', args=[c['args'][0], a1, mode & MASK]))
    for name, area in (('1E7D20 t 13 d0 p5 0 y200.0 f0', 0x13), ('1E7D20 t packets poisoned 6', 6)):
        c = by[name]
        blocks = list(c['script'][0x1CB5F0])
        blocks[31] = 0x008106F0          # the third header, 9 quadwords: blk+0x10 is D_00810700
        out.append(dict(c, name=f'1E7D20 r7 block over the area byte {area:x}',
                        script={**c['script'], 0x1CB5F0: blocks}))
    c = by['1A06A0 s edge narrowed x from hit']
    out.append(dict(c, name='1A06A0 r7 start x ffc00000 narrowed x from hit',
                    writes=list(c['writes']) + [W32(0x70003190, 0xFFC00000)]))
    c = by['1AA000 t keys 10200/200']
    for k in (0x201, 0x2FF):
        out.append(dict(c, name=f'1AA000 r7 keys {k:x}/{k:x}',
                        writes=list(c['writes']) + [W32(c['args'][2] + 0xE4, k), W32(c['args'][3] + 0xE4, k)]))
    c = by['1A9E00 t a0 byte 8']
    for v in (2, 0x80):
        out.append(dict(c, name=f'1A9E00 r7 a1+0x0B {v:x}', writes=list(c['writes']) + [W8(c['args'][1] + 0xB, v)]))
    for c in out:
        c['core'] = True                # the default run runs them too
        c['late'] = True                # not counted for store-site coverage (store_variants)
        c.pop('full_only', None)
    return out


def all_cases():
    rng = random.Random(0xA01)
    out = (cases_small(rng) + cases_owners(rng) + cases_world(rng) + cases_fx(rng) + cases_targeted(rng)
           + cases_probe(rng) + cases_synthetic(rng) + cases_edges(random.Random(0xED6E)) + cases_edges4()
           + cases_edges4b())
    out += cases_review5(out)
    out += cases_review6(out)
    return out + cases_review7(out)


# Every public entry: (address, arguments before the output, has an output).
API_ENTRIES = (
    (0x1287F0, (U32(0), U32(0), I32(0), U32(0)), False), (0x128B80, (U32(0), U32(0)), True),
    (0x128C10, (U32(0),), False), (0x157CE0, (U32(0), I32(0)), True), (0x158590, (U32(0), I32(0), I32(0)), False),
    (0x158D30, (U32(0),), False), (0x159B90, (U32(0),), False), (0x15A2C0, (U32(0),), False),
    (0x19B4C0, (U32(0), U32(0x100), U32(0x200), I32(0)), True), (0x19CF50, (), True), (0x1A06A0, (), True),
    (0x1A8840, (U32(0), U32(0)), False), (0x1A9E00, (U32(0), U32(0)), False),
    (0x1AA000, (U32(0), U32(0), U32(0), U32(0)), False), (0x1B0300, (), False), (0x1B0D80, (U32(0x7A8010),), True),
    (0x1B6D70, (U32(0), U32(0), U32(0)), True), (0x1B76D0, (U32(0), U32(0), U32(SCRATCH)), True),
    (0x1E3D90, (U32(0),), False), (0x1E7C60, (U32(0), U32(0)), False), (0x1E7CB0, (), True),
    (0x1E7D20, (U32(0),), False),
)


def api_checks():
    """The module's fail-stop contract (em_area01_sys.h), native only (the
    original has no counterpart): the fault code and address of an
    unmapped access (a word past the end of RAM or the scratchpad, or one
    straddling it; a region smaller than the access; a region with NULL
    bytes is skipped), a NULL worker and a negative worker result; for
    every entry a NULL context, and for every entry with a result a NULL
    output (code NULL with that entry, no work); clear_fault(NULL); the
    refusal of every call while a fault is latched (no worker call made,
    the latched fields unchanged) and the reset by
    em_area01_sys_clear_fault; and that the first fault is the one kept (a
    worker that re-enters the module, faults there, then fails). The
    register counts the worker is told are compared exactly in every case
    (NativeRun._call). The region-end reads that succeed are ordinary cases
    ('1B0D80 r5 region end'). Returns the number of checks."""
    ram, spad = image(BEATS[0])
    n = 0

    def fresh():
        return NativeRun(ram, spad, Script({}), set())

    def state(nat):
        return (nat.sys.fault, nat.sys.fault_function, nat.sys.fault_address)

    out = I32(0)
    # unmapped: past the end of RAM, straddling it, past the scratchpad
    for a0, at in ((0x02000000 - 0xB4, 0x02000000), (0x01FFFFFE - 0xB4, 0x01FFFFFE),
                   (0x70004000 - 0xB4, 0x70004000), (0x70003FFE - 0xB4, 0x70003FFE)):
        nat = fresh()
        rc = NATIVE.em_area01_sys_001B0D80(C.byref(nat.sys), U32(a0), C.byref(out))
        assert rc == -1 and state(nat) == (3, 0x1B0D80, at), ('api: unmapped read', hex(at), rc, state(nat))
        assert not nat.log, ('api: calls before the fault', nat.log)
        # latched: every later call is refused before any work
        rc = NATIVE.em_area01_sys_001B76D0(C.byref(nat.sys), U32(0), U32(0), U32(SCRATCH), C.byref(out))
        assert rc == -1 and not nat.log and state(nat) == (3, 0x1B0D80, at), ('api: refusal while latched', rc,
                                                                               nat.log, state(nat))
        NATIVE.em_area01_sys_clear_fault(C.byref(nat.sys))
        assert state(nat) == (0, 0, 0), ('api: clear_fault', state(nat))
        rc = NATIVE.em_area01_sys_001B76D0(C.byref(nat.sys), U32(0), U32(0), U32(SCRATCH), C.byref(out))
        assert rc == 0 and out.value == 1 and len(nat.log) == 1, ('api: call after clear_fault', rc, nat.log)
        n += 4
    # a NULL worker, a failing worker
    for worker, code in ((WORKER(), 1), (WORKER(lambda ctx, c: -1), 2)):
        nat = fresh()
        nat.sys.call = worker
        rc = NATIVE.em_area01_sys_001B76D0(C.byref(nat.sys), U32(0), U32(0), U32(SCRATCH), C.byref(out))
        assert rc == -1 and state(nat) == (code, 0x1B76D0, 0x1B1E20), ('api: worker fault', code, rc, state(nat))
        n += 1
    # every entry: a NULL context returns -1; an entry with a result and a
    # NULL output latches (NULL, the entry, 0) before any work
    for fn, args, has_out in API_ENTRIES:
        f = getattr(NATIVE, 'em_area01_sys_%08X' % fn)
        assert f(None, *args, *((C.byref(out),) if has_out else ())) == -1, ('api: NULL context', hex(fn))
        n += 1
        if has_out:
            nat = fresh()
            rc = f(C.byref(nat.sys), *args, None)
            assert rc == -1 and state(nat) == (1, fn, 0) and not nat.log, ('api: NULL output', hex(fn), rc,
                                                                           state(nat), nat.log)
            n += 1
    # clear_fault(NULL) does nothing
    NATIVE.em_area01_sys_clear_fault(None)
    n += 1
    # regions: one with NULL bytes is skipped (the next region serves the
    # address); one smaller than the access refuses it (UNMAPPED at the
    # access address), also when the access starts at its base
    buf = (U8 * 0x1000)()
    struct.pack_into('<f', buf, 0x1B4, -300.0)
    regs = (Region * 2)(Region(0, 0x1000, None), Region(0, 0x1000, C.cast(buf, P(U8))))
    nat = fresh()
    nat.sys.regions, nat.sys.region_count = C.cast(regs, P(Region)), 2
    rc = NATIVE.em_area01_sys_001B0D80(C.byref(nat.sys), U32(0x100), C.byref(out))
    assert rc == 0 and out.value == 1 and buf[0x104] == 3 and state(nat) == (0, 0, 0), ('api: NULL-bytes region',
                                                                                     rc, out.value, buf[0x104])
    tiny = (U8 * 2)()
    for a0 in (0x1000 - 0xB4, 0x1001 - 0xB4):
        regs1 = (Region * 1)(Region(0x1000, 2, C.cast(tiny, P(U8))))
        nat = fresh()
        nat.sys.regions, nat.sys.region_count = C.cast(regs1, P(Region)), 1
        rc = NATIVE.em_area01_sys_001B0D80(C.byref(nat.sys), U32(a0), C.byref(out))
        assert rc == -1 and state(nat) == (3, 0x1B0D80, a0 + 0xB4), ('api: region smaller than the access',
                                                                     hex(a0), rc, state(nat))
        n += 1
    n += 1
    # (round 7) an access that is exactly a whole region (it starts at the
    # region base and ends at its end) is served
    four = (U8 * 4)()
    regs4 = (Region * 1)(Region(0x1000, 4, C.cast(four, P(U8))))
    nat = fresh()
    nat.sys.regions, nat.sys.region_count = C.cast(regs4, P(Region)), 1
    out.value = 7
    rc = NATIVE.em_area01_sys_001B0D80(C.byref(nat.sys), U32(0x1000 - 0xB4), C.byref(out))
    assert rc == 0 and out.value == 0 and state(nat) == (0, 0, 0), ('api: access at a region base', rc, out.value,
                                                                    state(nat))
    n += 1
    # (round 7) bytes stored before a fault stay stored: 00128B80 stores
    # +0, +4..+7 and then calls 0012E070, whose worker fails
    nat = fresh()
    rec = SCRATCH + 0x100
    struct.pack_into('<H', nat.ram, rec + 0x36, 1)
    for k in (0, 4, 5, 6, 7):
        nat.ram[rec + k] = 0x5A
    nat.sys.call = WORKER(lambda ctx, c: -1)
    rc = NATIVE.em_area01_sys_00128B80(C.byref(nat.sys), U32(rec), U32(0), C.byref(out))
    assert rc == -1 and state(nat) == (2, 0x128B80, 0x12E070) and \
        [nat.ram[rec + k] for k in (0, 4, 5, 6, 7)] == [3, 2, 0, 0, 0], ('api: bytes written before a fault', rc,
                                                                         state(nat))
    n += 1
    # (round 7) any fault value other than NONE is refused, not only the
    # codes the module sets: no worker call, the fields unchanged
    for code in (-1, -5, -0x80000000):
        nat = fresh()
        nat.sys.fault, nat.sys.fault_function, nat.sys.fault_address = code, 0x123, 0x456
        rc = NATIVE.em_area01_sys_001B76D0(C.byref(nat.sys), U32(0), U32(0), U32(SCRATCH), C.byref(out))
        assert rc == -1 and not nat.log and state(nat) == (code, 0x123, 0x456), ('api: refusal of fault value',
                                                                                 code, rc, nat.log, state(nat))
        n += 1
    # the first fault is kept: the worker re-enters the module, which faults
    # (unmapped), and then fails; the outer call must keep the inner fault
    nat = fresh()
    inner = I32(0)

    def reenter(ctx, c):
        NATIVE.em_area01_sys_001B0D80(C.byref(nat.sys), U32(0x02000000 - 0xB4), C.byref(inner))
        return -1
    nat.sys.call = WORKER(reenter)
    rc = NATIVE.em_area01_sys_001B76D0(C.byref(nat.sys), U32(0), U32(0), U32(SCRATCH), C.byref(out))
    assert rc == -1 and state(nat) == (3, 0x1B0D80, 0x02000000), ('api: first fault kept', rc, state(nat))
    n += 1
    return n


# ======================================================================
# Translations of lane functions that live in other port modules: the port
# already has 00191120 (em_camera_follow_original.c), 001B0C00
# (em_script_host_workers.c) and 001FAD70 (em_stream_lanes_original.c).
# They are not duplicated here; a bridge (written into the private build
# directory) calls them unchanged and the original runs beside them.
# ======================================================================

BRIDGE = r"""#include <string.h>
#include "game/em_stream_lanes_original.h"
#include "game/em_camera_follow_original.h"
#include "game/em_script_host_workers.h"

typedef int (*wrap_fn)(void *, uint32_t, uint32_t *);
typedef int (*fade_fn)(void *, int32_t, int32_t);
typedef int (*lane_fn)(void *, int32_t, int32_t, int32_t);

int a01sys_191120(wrap_fn wrap, uint32_t goal, uint32_t cur, uint32_t rate, uint32_t limit, uint32_t *out);
int a01sys_1B0C00(fade_fn fade, lane_fn lane, int32_t a0);
int a01sys_1FAD70(uint8_t *ram, int32_t lane, int32_t fade, int32_t release);

int a01sys_191120(wrap_fn wrap, uint32_t goal, uint32_t cur, uint32_t rate, uint32_t limit, uint32_t *out)
{
    EmCameraFollowWorkers w;
    memset(&w, 0, sizeof w);
    w.wrap = wrap;
    return em_camera_follow_00191120(&w, goal, cur, rate, limit, out);
}

int a01sys_1B0C00(fade_fn fade, lane_fn lane, int32_t a0)
{
    static EmScriptHostWorkers h;
    memset(&h, 0, sizeof h);
    h.callees.w_001AEDE0 = fade;
    h.callees.w_001FAD70 = lane;
    return em_script_host_001B0C00(&h, a0);
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* Loads the lane bytes 001FAD70 reads from the RAM image, runs the port's
 * translation, and stores the bytes it owns back. */
int a01sys_1FAD70(uint8_t *ram, int32_t lane, int32_t fade, int32_t release)
{
    static EmStreamLanes L;
    static EmStreamLanesGlobals g;
    static EmStreamLanesData d;
    EmStreamLanesWorkers w;
    int i, rc;
    memset(&L, 0, sizeof L);
    memset(&g, 0, sizeof g);
    memset(&d, 0, sizeof d);
    memset(&w, 0, sizeof w);
    em_stream_lanes_bind(&L, &d, &g, &w);
    for (i = 0; i < EM_STREAM_LANES; i++) {
        uint8_t *rec = ram + 0x281FD0 + i * 0x60;
        L.state.active[i] = (int8_t)ram[0x282154 + i];
        L.state.lane[i].release = (int8_t)rec[0x5C];
        L.state.lane[i].fade_step = rd32(rec + 0x54);
    }
    rc = em_stream_lanes_001FAD70(&L, lane, fade, release);
    for (i = 0; i < EM_STREAM_LANES; i++) {
        uint8_t *rec = ram + 0x281FD0 + i * 0x60;
        uint32_t v = L.state.lane[i].fade_step;
        rec[0x5C] = (uint8_t)L.state.lane[i].release;
        rec[0x54] = (uint8_t)v; rec[0x55] = (uint8_t)(v >> 8); rec[0x56] = (uint8_t)(v >> 16); rec[0x57] = (uint8_t)(v >> 24);
    }
    return rc;
}
"""
EXISTING_SOURCES = ['src/game/em_stream_lanes_original.c', 'src/game/em_camera_follow_original.c',
                    'src/game/em_sdk_math_original.c', 'src/game/em_script_host_workers.c',
                    'src/game/em_player_stage_workers.c', 'src/game/em_script.c']
WRAP_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, U32, P(U32))
FADE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, I32, I32)
LANE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, I32, I32, I32)


def build_existing():
    OUT.mkdir(parents=True, exist_ok=True)
    bridge = OUT / 'bridge_existing.c'
    if not bridge.exists() or bridge.read_text() != BRIDGE:
        bridge.write_text(BRIDGE)
    lib = OUT / ('existing.dylib' if sys.platform == 'darwin' else 'existing.so')
    if stale(lib, EXISTING_SOURCES + [str(bridge.relative_to(ROOT))]):
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-ffp-contract=off',
                        '-shared', '-fPIC', '-Isrc', *EXISTING_SOURCES, str(bridge), '-o', str(lib)],
                       cwd=ROOT, check=True)
    n = C.CDLL(str(lib))
    n.a01sys_191120.argtypes = [WRAP_FN, U32, U32, U32, U32, P(U32)]
    n.a01sys_1B0C00.argtypes = [FADE_FN, LANE_FN, I32]
    n.a01sys_1FAD70.argtypes = [P(U8), I32, I32, I32]
    return n


def existing_checks(rng):
    """Returns the number of cases compared."""
    n = build_existing()
    ram, spad = image(BEATS[0])
    count = 0
    # 00191120(goal, current, rate, limit) -> f0; 001B1470 runs as original
    # on both sides (the port calls it through its wrap worker; it takes the
    # absolute value 0011DF78 inline).
    import math
    angles = [0.0, 1.0, -1.0, 3.1, -3.1, 3.14159274, -3.14159274, 0.5, 2.0]
    for i in range(RM.pick(300, 60)):
        goal = rng.choice(angles + [rng.uniform(-3.2, 3.2)])
        cur = rng.choice([goal, goal + 0.001, goal - 0.3] + angles + [rng.uniform(-3.2, 3.2)])
        rate = rng.choice((0.0349066, 0.1, 1.0, 0.0))
        limit = rng.choice((0.0, 0.01, 0.5, 4.0))
        fl = [FB(goal), FB(cur), FB(rate), FB(limit)]
        ee = SysEE(ELF, ram, spad)
        olog = []

        def wrap_hook(e):
            olog.append(e.f[12] & MASK)
            run_nested(e, 0x1B1470)
        ee.hooks = {0x1B1470: wrap_hook}
        for k, v in enumerate(fl):
            ee.f[12 + k] = v
        ee.r[29], ee.r[31] = SP, shared.RETURN
        ee.run(0x191120)
        want = ee.f[0] & MASK
        nee, nlog = SysEE(ELF, ram, spad), []

        def wrap(_, x, out):
            nlog.append(x)
            nee.f[12] = x
            nee.r[29] = SP
            out[0] = run_nested(nee, 0x1B1470)[1] & MASK
            return 0
        cb = WRAP_FN(wrap)
        got = U32(0)
        assert n.a01sys_191120(cb, *fl, C.byref(got)) == 0
        assert got.value == want and nlog == olog, ('00191120', goal, cur, rate, limit, hex(got.value), hex(want),
                                                    nlog, olog)
        count += 1
    # 001B0C00(a0): the two callees stubbed and logged on both sides
    for a0 in (0, 1, 30, 0x7B20F0, -1):
        ee = SysEE(ELF, ram, spad)
        olog = []
        ee.hooks = {0x1AEDE0: lambda e: olog.append(('1AEDE0', e.r[4] & MASK, e.r[5] & MASK)),
                    0x1FAD70: lambda e: olog.append(('1FAD70', e.r[4] & MASK, e.r[5] & MASK, e.r[6] & MASK))}
        ee.r[4], ee.r[29], ee.r[31] = sx32(a0) & MASK64, SP, shared.RETURN
        ee.run(0x1B0C00)
        nlog = []
        f = FADE_FN(lambda _, x, y: nlog.append(('1AEDE0', x & MASK, y & MASK)) or 0)
        g = LANE_FN(lambda _, x, y, z: nlog.append(('1FAD70', x & MASK, y & MASK, z & MASK)) or 0)
        assert n.a01sys_1B0C00(f, g, a0) == 0
        assert nlog == olog, ('001B0C00', a0, nlog, olog)
        count += 1
    # 001FAD70(lane, fade, release) over the captured lane records: all RAM
    for i in range(RM.pick(240, 48)):
        lane = i % 3
        fade = rng.choice((0, 1, 30, 120, 16383, -7, rng.randrange(1, 100000)))
        release = rng.choice((0, 1, 0xFF, 0x1FF, -1))
        work = bytearray(ram)
        work[0x282154 + lane] = rng.choice((0, 1, 0x80))
        ee = SysEE(ELF, bytes(work), spad)
        ee.r[4], ee.r[5], ee.r[6] = sx32(lane) & MASK64, sx32(fade) & MASK64, sx32(release) & MASK64
        ee.r[29], ee.r[31] = SP, shared.RETURN
        ee.run(0x1FAD70)
        buf = (U8 * len(work)).from_buffer(work)
        assert n.a01sys_1FAD70(buf, lane, fade, release) == 0
        del buf
        assert bytes(work) == bytes(ee.mem), ('001FAD70', lane, fade, release,
                                              first_differences(bytes(work), bytes(ee.mem)))
        count += 1
    return count


# ======================================================================
# Measured register reads of every callee
# ======================================================================
#
# The call log compares, per callee, the integer argument registers a0..
# (CALLEES count) and the float argument registers f12.. (count). RegScan
# measures which registers each callee really reads before writing them,
# over every path from its entry to its return and through everything it
# calls, from the instruction words of the pinned ELF; main() fails when a
# callee reads an argument register its policy does not compare, or any
# other register its caller would have to supply (the temporaries, v0/v1,
# the assembler temporary, the callee-saved registers other than to save
# them, the float temporaries).

# Hidden inputs are tracked as pseudo registers: integer 32..35 = HI, LO,
# HI1, LO1 (the EE's second multiply pipeline), float 32 = the COP1
# condition flag, 33 = the COP1 accumulator. None is ever passed by a
# caller, so a callee reading one before writing it fails the test. The
# VU0 state is tracked in the float mask from bit 64 on, per lane: vf0..31
# x/y/z/w, vi0..15, the VU0 accumulator x/y/z/w, Q, I, R and one bit for
# the MAC / status / clip flags. vf0 and vi0 are constant (defined at
# entry); a write through a partial dest mask defines only its lanes.
HI, LO, HI1, LO1 = 32, 33, 34, 35
FCC, FACC = 32, 33
VU_BASE = 64


def VF(reg, lane):
    return VU_BASE + reg * 4 + lane


def VI(reg):
    return VU_BASE + 128 + (reg & 15)


VACC = [VU_BASE + 144 + lane for lane in range(4)]
VQ, VIR, VR, VFLAGS = VU_BASE + 148, VU_BASE + 149, VU_BASE + 150, VU_BASE + 151
VU_ALL = list(range(VU_BASE, VU_BASE + 152))
ENTRY_F = sum(1 << VF(0, lane) for lane in range(4)) | (1 << VI(0))
SAVED_I = set(range(16, 24)) | {30, 31}
SAVED_F = set(range(20, 32))
ENTRY_I = (1 << 0) | (1 << 28) | (1 << 29) | (1 << 31)   # zero, gp, sp, ra hold defined values
CLOBBER_I = sum(1 << r for r in list(range(1, 16)) + [24, 25, 31, HI, LO, HI1, LO1])
CLOBBER_F = sum(1 << r for r in list(range(0, 20)) + [FCC, FACC] + VU_ALL)

# Indirect jumps whose target the scan cannot know from the ELF, with the
# argument registers the target is taken to read (integer, float) and why.
# Every other indirect jump must resolve to a table; a new unresolved one
# fails the test.
UNRESOLVED_JUMPS = {
    0x1B99F4: (3, 0, 'script command 9 handler 001B99F0 (reached from 001BA1F0 through its '
                     'command table at 0x0024D880) jumps to a function pointer held in the '
                     'script record (word +4), with a0..a2 = the dispatcher\'s (actor, '
                     'script, record); the target is script data, not code the ELF names'),
}


def reg_effects(word):
    """(integer registers read, written, float registers read, written,
    control kind) of one EE instruction word."""
    op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
    fs, ft, fd, funct = rd, rt, word >> 6 & 31, word & 63
    ir, iw, fr, fw, kind = [], [], [], [], None
    if op == 0:
        if funct in (0, 2, 3, 0x38, 0x3A, 0x3B, 0x3C, 0x3E, 0x3F):
            ir, iw = [rt], [rd]
        elif funct in (4, 6, 7, 0x14, 0x16, 0x17):
            ir, iw = [rt, rs], [rd]
        elif funct == 8:
            ir, kind = [rs], ('return' if rs == 31 else 'jump_reg')
        elif funct == 9:
            ir, iw, kind = [rs], [rd], 'call_reg'
        elif funct in (0xA, 0xB):                    # conditional moves keep rd otherwise
            ir, iw = [rs, rt, rd], [rd]
        elif funct in (0xC, 0xD, 0xF):
            pass
        elif funct in (0x10, 0x12):                  # mfhi / mflo
            ir, iw = [HI if funct == 0x10 else LO], [rd]
        elif funct == 0x28:
            iw = [rd]
        elif funct in (0x11, 0x13):                  # mthi / mtlo
            ir, iw = [rs], [HI if funct == 0x11 else LO]
        elif funct == 0x29:
            ir = [rs]
        elif funct in (0x18, 0x19):                  # three-operand multiply
            ir, iw = [rs, rt], [rd, HI, LO]
        elif funct in (0x1A, 0x1B):
            ir, iw = [rs, rt], [HI, LO]
        elif 0x30 <= funct <= 0x36:
            ir = [rs, rt]
        elif 0x20 <= funct <= 0x2F:
            ir, iw = [rs, rt], [rd]
        else:
            raise ValueError(('unknown special function', hex(word)))
    elif op == 1:
        ir = [rs]
        if rt == 1 and rs == 0:
            ir, kind = [], 'always'
        elif rt in (0, 1):
            kind = 'cond'
        elif rt in (2, 3):
            kind = 'likely'
        elif rt in (0x10, 0x11):
            kind, iw = 'cond', [31]
        elif rt in (0x12, 0x13):
            kind, iw = 'likely', [31]
    elif op == 2:
        kind = 'jump'
    elif op == 3:
        kind = 'call'
    elif op == 4 and rs == rt:
        kind = 'always'
    elif op in (4, 5):
        ir, kind = [rs, rt], 'cond'
    elif op in (20, 21):
        ir, kind = [rs, rt], 'likely'
    elif op in (6, 7):
        ir, kind = [rs], 'cond'
    elif op in (22, 23):
        ir, kind = [rs], 'likely'
    elif 8 <= op <= 14 or op in (24, 25, 30) or op in (32, 33, 35, 36, 37, 39, 55):
        ir, iw = [rs], [rt]
    elif op == 15:
        iw = [rt]
    elif op in (26, 27, 34, 38):                     # partial loads merge into rt
        ir, iw = [rs, rt], [rt]
    elif op == 28:                                   # MMI
        ir, iw = [rs, rt], [rd]
        sa = word >> 6 & 31
        if funct in (0, 1):                          # madd / maddu
            ir, iw = [rs, rt, HI, LO], [rd, HI, LO]
        elif funct in (0x20, 0x21):
            ir, iw = [rs, rt, HI1, LO1], [rd, HI1, LO1]
        elif funct in (0x10, 0x12):
            ir, iw = [HI1 if funct == 0x10 else LO1], [rd]
        elif funct in (0x11, 0x13):
            ir, iw = [rs], [HI1 if funct == 0x11 else LO1]
        elif funct in (0x18, 0x19):
            ir, iw = [rs, rt], [rd, HI1, LO1]
        elif funct in (0x1A, 0x1B):
            ir, iw = [rs, rt], [HI1, LO1]
        elif funct == 0x30 or (funct in (0x09, 0x29) and sa in (0x00, 0x04, 0x10, 0x11, 0x14, 0x15)) \
                or (funct == 0x09 and sa in (0x08, 0x09)):
            ir, iw = [rs, rt, HI, LO, HI1, LO1], [rd, HI, LO, HI1, LO1]   # reads (and keeps) all four
        elif funct == 0x31 or (funct in (0x09, 0x29) and sa in (0x08, 0x09, 0x0C, 0x0D, 0x0E, 0x1C)):
            iw = [rd, HI, LO, HI1, LO1]
    elif op in (31, 40, 41, 42, 43, 44, 45, 46, 63):
        ir = [rs] if rs == 29 and rt in SAVED_I else [rs, rt]    # saving a callee-saved register
    elif op in (47, 51):
        ir = [rs]
    elif op == 54:                                   # lqc2
        ir, fw = [rs], [VF(rt, lane) for lane in range(4)]
    elif op == 62:                                   # sqc2
        ir, fr = [rs], [VF(rt, lane) for lane in range(4)]
    elif op == 49:
        ir, fw = [rs], [ft]
    elif op == 57:
        ir, fr = [rs], ([] if rs == 29 and ft in SAVED_F else [ft])
    elif op == 16:
        if rs == 0:
            iw = [rt]
        elif rs == 4:
            ir = [rt]
    elif op == 17:
        if rs == 0:
            fr, iw = [fs], [rt]
        elif rs == 2:                                # cfc1 (the control word holds the flag)
            iw, fr = [rt], [FCC]
        elif rs == 4:
            ir, fw = [rt], [fs]
        elif rs == 6:
            ir, fw = [rt], [FCC]
        elif rs == 8:
            fr, kind = [FCC], ('likely' if rt & 2 else 'cond')
        elif rs == 16:
            if funct in (0, 1, 2, 3, 0x16, 0x28, 0x29):
                fr, fw = [fs, ft], [fd]
            elif funct in (0x1C, 0x1D):              # madd.s / msub.s: ACC +/- fs * ft
                fr, fw = [fs, ft, FACC], [fd]
            elif funct == 4:
                fr, fw = [ft], [fd]
            elif funct in (5, 6, 7, 0x24):
                fr, fw = [fs], [fd]
            elif funct in (0x18, 0x19, 0x1A):        # adda / suba / mula
                fr, fw = [fs, ft], [FACC]
            elif funct in (0x1E, 0x1F):              # madda / msuba
                fr, fw = [fs, ft, FACC], [FACC]
            elif funct in (0x30, 0x32, 0x34, 0x36):
                fr, fw = [fs, ft], [FCC]
            else:
                raise ValueError(('unknown single-precision function', hex(word)))
        elif rs == 20:
            fr, fw = [fs], [fd]
        else:
            raise ValueError(('unknown coprocessor-1 form', hex(word)))
    elif op == 18:
        if word >> 25 & 1:
            fr, fw = vu0_macro(word)
        elif rs == 1:                                # qmfc2
            iw, fr = [rt], [VF(rd, lane) for lane in range(4)]
        elif rs == 2:                                # cfc2
            iw, fr = [rt], [VI(rd) if rd < 16 else {20: VR, 21: VIR, 22: VQ}.get(rd, VFLAGS)]
        elif rs == 5:                                # qmtc2
            ir, fw = [rt], [VF(rd, lane) for lane in range(4)]
        elif rs == 6:                                # ctc2
            ir, fw = [rt], [VI(rd) if rd < 16 else {20: VR, 21: VIR, 22: VQ}.get(rd, VFLAGS)]
        elif rs == 8:
            fr, kind = [VFLAGS], ('likely' if rt & 2 else 'cond')
        else:
            raise ValueError(('unknown coprocessor-2 form', hex(word)))
    else:
        raise ValueError(('unknown opcode', hex(word)))
    return ir, iw, fr, fw, kind


def vu0_macro(word):
    """(state read, state written) of one VU0 macro instruction, in the
    VF / VI / VACC / VQ / VIR / VR / VFLAGS numbering. Every arithmetic
    operation also sets the MAC / status flags (as a write: the result does
    not depend on the old flags; only vclipw, cfc2 and bc2 read them)."""
    fs, ft, fd, funct = word >> 11 & 31, word >> 16 & 31, word >> 6 & 31, word & 63
    lanes = [lane for lane in range(4) if word >> (24 - lane) & 1]
    fsf, ftf = word >> 21 & 3, word >> 23 & 3
    xyz = [0, 1, 2]

    def v(reg, ls):
        return [VF(reg, lane) for lane in ls]

    def acc(ls):
        return [VACC[lane] for lane in ls]

    fl = [VFLAGS]
    if funct < 0x3C:
        if funct < 0x1C:
            grp, bc = funct >> 2, funct & 3
            rd = v(fs, lanes) + [VF(ft, bc)] + (acc(lanes) if grp in (2, 3) else [])
            return rd, v(fd, lanes) + fl
        if funct in (0x1C, 0x20, 0x21, 0x24, 0x25):
            return v(fs, lanes) + [VQ] + (acc(lanes) if funct in (0x21, 0x25) else []), v(fd, lanes) + fl
        if funct in (0x1D, 0x1E, 0x1F, 0x22, 0x23, 0x26, 0x27):
            return v(fs, lanes) + [VIR] + (acc(lanes) if funct in (0x23, 0x27) else []), v(fd, lanes) + fl
        if funct in (0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2F):
            return v(fs, lanes) + v(ft, lanes) + (acc(lanes) if funct in (0x29, 0x2D) else []), v(fd, lanes) + fl
        if funct == 0x2E:                            # vopmsub
            return v(fs, xyz) + v(ft, xyz) + acc(xyz), v(fd, lanes) + fl
        if funct in (0x30, 0x31, 0x34, 0x35):        # viadd / visub / viand / vior
            return [VI(fs), VI(ft)], [VI(fd)]
        if funct == 0x32:                            # viaddi
            return [VI(fs)], [VI(ft)]
        if funct in (0x38, 0x39):                    # vcallms / vcallmsr: a microprogram
            return VU_ALL, []
        raise ValueError(('unknown VU0 macro', hex(word)))
    op = fd << 2 | (funct & 3)
    if op < 0x10 or 0x18 <= op < 0x1C:
        bc = op & 3
        return v(fs, lanes) + [VF(ft, bc)] + (acc(lanes) if 0x08 <= op < 0x10 else []), acc(lanes) + fl
    if 0x10 <= op < 0x18 or op == 0x1D:              # vitof / vftoi / vabs
        return v(fs, lanes), v(ft, lanes)
    if op in (0x1C, 0x20, 0x21, 0x24, 0x25):
        return v(fs, lanes) + [VQ] + (acc(lanes) if op in (0x21, 0x25) else []), acc(lanes) + fl
    if op in (0x1E, 0x22, 0x23, 0x26, 0x27):
        return v(fs, lanes) + [VIR] + (acc(lanes) if op in (0x23, 0x27) else []), acc(lanes) + fl
    if op == 0x1F:                                   # vclipw
        return v(fs, xyz) + [VF(ft, 3)] + fl, fl
    if op in (0x28, 0x29, 0x2A, 0x2C, 0x2D):
        return v(fs, lanes) + v(ft, lanes) + (acc(lanes) if op in (0x29, 0x2D) else []), acc(lanes) + fl
    if op == 0x2E:                                   # vopmula
        return v(fs, xyz) + v(ft, xyz), acc(xyz) + fl
    if op in (0x2F, 0x3B):                           # vnop / vwaitq
        return [], []
    if op == 0x30:                                   # vmove
        return v(fs, lanes), v(ft, lanes)
    if op == 0x31:                                   # vmr32 (rotated lanes)
        return v(fs, range(4)), v(ft, lanes)
    if op in (0x34, 0x36):                           # vlqi / vlqd
        return [VI(fs)], v(ft, lanes) + [VI(fs)]
    if op in (0x35, 0x37):                           # vsqi / vsqd
        return v(fs, lanes) + [VI(ft)], [VI(ft)]
    if op == 0x38:                                   # vdiv
        return [VF(fs, fsf), VF(ft, ftf)], [VQ] + fl
    if op == 0x39:                                   # vsqrt
        return [VF(ft, ftf)], [VQ] + fl
    if op == 0x3A:                                   # vrsqrt
        return [VF(fs, fsf), VF(ft, ftf)], [VQ] + fl
    if op == 0x3C:                                   # vmtir
        return [VF(fs, fsf)], [VI(ft)]
    if op == 0x3D:                                   # vmfir
        return [VI(fs)], v(ft, lanes)
    if op == 0x3E:                                   # vilwr
        return [VI(fs)], [VI(ft)]
    if op == 0x3F:                                   # viswr
        return [VI(fs), VI(ft)], []
    if op in (0x40, 0x41):                           # vrnext / vrget
        return [VR], v(ft, lanes) + ([VR] if op == 0x40 else [])
    if op in (0x42, 0x43):                           # vrinit / vrxor
        return [VF(fs, fsf)] + ([VR] if op == 0x43 else []), [VR]
    raise ValueError(('unknown VU0 macro', hex(word)))


class RegScan:
    """Registers read before written, per function, by a forward must-write
    analysis over every path (branch delay slots included; a likely
    branch's slot only on the taken path), following jumps, jump tables and
    calls (a call adds what the callee reads, then clobbers the caller-saved
    registers). Returns (integer mask, float mask, unresolved sites)."""

    def __init__(self, mem, lo=0x100000, hi=0x275B00):
        self.mem, self.lo, self.hi, self.memo = mem, lo, hi, {}
        self.first = {}           # (function, register) -> first reading pc

    def word(self, pc):
        if not self.lo <= pc < self.hi:
            raise ValueError(('scan left the code', hex(pc)))
        return struct.unpack_from('<I', self.mem, pc)[0]

    def table(self, site, reg):
        """The entries of the table an indirect jump / call at `site` loads
        its target from, found by folding constants over the preceding 16
        words (a base from an upper-half load plus a low half, an index
        added to it, one word loaded from it). The count is the bound of the
        preceding unsigned compare with an immediate; without one, the run
        of entries that point into the code. None when not resolvable."""
        const, bound = {}, None
        for pc in range(site - 64, site, 4):
            w = self.word(pc)
            op, rs, rt, rd = w >> 26, w >> 21 & 31, w >> 16 & 31, w >> 11 & 31
            imm = w & 0xFFFF
            simm = imm - ((imm & 0x8000) << 1)
            dst, val = None, None
            if op == 15:
                dst, val = rt, ('c', (imm << 16) & MASK)
            elif op == 9 and const.get(rs, ('',))[0] == 'c':
                dst, val = rt, ('c', (const[rs][1] + simm) & MASK)
            elif op == 11:
                dst, bound = rt, imm
            elif op == 0 and (w & 63) in (0x21, 0x2D):
                a, b = const.get(rs), const.get(rt)
                dst = rd
                if a and a[0] == 'c' and not b:
                    val = ('b', a[1])
                elif b and b[0] == 'c' and not a:
                    val = ('b', b[1])
            elif op == 35 and const.get(rs, ('',))[0] == 'b':
                dst, val = rt, ('t', (const[rs][1] + simm) & MASK)
            else:
                for r in reg_effects(w)[1]:
                    const.pop(r, None)
            if dst is not None:
                if val is None:
                    const.pop(dst, None)
                else:
                    const[dst] = val
        v = const.get(reg)
        if not v or v[0] != 't':
            return None
        out = []
        while bound is None or len(out) < bound:
            e = struct.unpack_from('<I', self.mem, v[1] + 4 * len(out))[0]
            if not (self.lo <= e < self.hi and e % 4 == 0):
                break
            out.append(e)
        return None if bound is not None and len(out) != bound else out

    def reads(self, fn, active=()):
        if fn in self.memo:
            return self.memo[fn]
        if fn in active:
            return (0, 0, {('recursion', fn)})
        active = active + (fn,)
        ri = rf = 0
        notes = set()
        state = {fn: (ENTRY_I, ENTRY_F)}
        work = [fn]

        def step(pc, si, sf):
            nonlocal ri, rf
            ir, iw, fr, fw, kind = reg_effects(self.word(pc))
            for r in ir:
                if not si >> r & 1:
                    ri |= 1 << r
                    self.first.setdefault((fn, r), pc)
            for r in fr:
                if not sf >> r & 1:
                    rf |= 1 << r
                    self.first.setdefault((fn, 64 + r), pc)
            for r in iw:
                si |= 1 << r
            for r in fw:
                sf |= 1 << r
            return si, sf, kind

        def push(pc, si, sf):
            old = state.get(pc)
            new = (si, sf) if old is None else (old[0] & si, old[1] & sf)
            if new != old:
                state[pc] = new
                work.append(pc)

        def callee_reads(ci, cf, si, sf):
            nonlocal ri, rf
            ri |= ci & ~si
            rf |= cf & ~sf

        while work:
            pc = work.pop()
            word = self.word(pc)
            si, sf, kind = step(pc, *state[pc])
            if kind is None:
                push(pc + 4, si, sf)
                continue
            di, df, k2 = step(pc + 4, si, sf)            # the delay slot
            assert k2 is None, ('control transfer in a delay slot', hex(pc))
            if kind in ('cond', 'likely', 'always'):
                push(pc + 4 + ((word & 0xFFFF) - ((word & 0x8000) << 1)) * 4, di, df)
                if kind == 'cond':
                    push(pc + 8, di, df)
                elif kind == 'likely':
                    push(pc + 8, si, sf)
            elif kind == 'jump':
                push((pc & 0xF0000000) | ((word & 0x3FFFFFF) << 2), di, df)
            elif kind == 'jump_reg':
                targets = self.table(pc, word >> 21 & 31)
                if targets is None:
                    notes.add(('unresolved jump', pc))
                    na, nf = UNRESOLVED_JUMPS.get(pc, (8, 4, ''))[:2]
                    callee_reads(((1 << na) - 1) << 4, ((1 << nf) - 1) << 12, di, df)
                for t in targets or ():
                    push(t, di, df)
            elif kind in ('call', 'call_reg'):
                if kind == 'call':
                    targets = [(pc & 0xF0000000) | ((word & 0x3FFFFFF) << 2)]
                else:
                    targets = self.table(pc, word >> 21 & 31)
                ci = cf = 0
                if targets is None:
                    # an unknown target may read every argument register;
                    # only the ones this path has not written are inputs
                    notes.add(('unresolved call', pc))
                    ci, cf = 0xFF0, 0xF000
                for t in targets or ():
                    a, b, n = self.reads(t, active)
                    ci, cf = ci | a, cf | b
                    notes |= n
                callee_reads(ci, cf, di, df)
                push(pc + 8, di | CLOBBER_I, df | CLOBBER_F)
        notes = {n for n in notes if not (n[0] == 'recursion' and n[1] == fn)}
        if not any(n[0] == 'recursion' for n in notes):
            self.memo[fn] = (ri, rf, notes)
        return ri, rf, notes


# Callees (reached from a stubbed callee) that read a caller-saved register
# (s0..s7, fp, f20..f31) other than to save it, i.e. a value their caller
# never passed. Each is on a path the scan cannot rule out; none is reached
# from a run-policy callee (which the test executes), which main() asserts.
CALLER_REG_READS = {
    0x15A750: 's1 at 0x15A7EC when its record halfword +0x54 is 7 or more (its switch bound): the '
              'particle count stays unset',
    0x17B490: 's0 at 0x17B590 when its switch index is 7 or more',
    0x18CBD0: 'f20 at 0x18CD98 on the path where the float compare at 0x18CD44 skips both writes of f20',
    0x19C830: 's0, s2, s3 at 0x19C97C..0x19C98C on the paths that reach its tail without the block '
              'that sets them (0x19C954..0x19C95C)',
    0x19CB60: 's1, s2, s4: the same three registers at the same point of the walk as 0019CF50 '
              '(no cell span beats the word 0x7000320C)',
    0x19CF50: 's1, s2, s4 when no cell span beats the word 0x7000320C (translated here: the module '
              'faults UNDEFINED there)',
    0x19D330: 's1, s2, s4: as 0019CB60',
    0x19D770: 's1, s2, s4: as 0019CB60',
    0x19E280: 's1, s2, s4: as 0019CB60',
    0x1CFBE0: 's0, s1 at 0x1CFF0C / 0x1CFF24 when its a1 is 7 or more (its switch bound); this '
              'module passes 1 or 6',
}
# Stubbed callees that read VU0 state their caller never set, with why the
# read is not an input. Enforced exactly (a new one fails the test).
VU_READS = {
    0x129780: ({VF(6, 3)}, 'through the cross-product leaf 00102718: it reads vf6.w only to subtract it '
                           'from itself (the w lane of the result is cleared); VU0 arithmetic has no '
                           'NaN or infinity, so the lane is +0 whatever vf6.w held'),
    0x12D580: ({VF(6, 3)}, 'the same leaf 00102718'),
}
SAVED_READ_MASK_I = sum(1 << r for r in range(16, 24)) | (1 << 30)
SAVED_READ_MASK_F = sum(1 << r for r in range(20, 32))


def check_register_reads(scan, indirect_targets):
    """Fails when a callee reads a register its policy does not compare, a
    caller temporary, or (for a callee the test executes) a caller-saved
    register; and when the caller-saved reads differ from CALLER_REG_READS.
    Returns [(callee, compared, measured)] for the report."""
    rows, bad, unresolved = [], [], set()
    items = [(fn, pol) for fn, pol in sorted(CALLEES.items())]
    items += [(fn, INDIRECT) for fn in sorted(indirect_targets) if fn not in CALLEES]
    for fn, pol in items:
        ri, rf, notes = scan.reads(fn)
        kind, na, nf = pol[0], pol[1], pol[2]
        ints = {r for r in range(36) if ri >> r & 1}
        floats = {r for r in range(VU_BASE + 152) if rf >> r & 1}
        extra_i = sorted(r for r in ints if not 4 <= r < 4 + na and r not in (0, 28, 29, 31)
                         and not (kind == S and SAVED_READ_MASK_I >> r & 1))
        vu_ok = VU_READS.get(fn, (set(),))[0] if kind == S else set()
        vu_read = {r for r in floats if r >= VU_BASE}
        if vu_read & vu_ok != vu_ok:
            bad.append((hex(fn), 'documented VU0 read no longer measured', sorted(vu_ok - vu_read)))
        extra_f = sorted(r for r in floats if not 12 <= r < 12 + nf and not (kind == S and 20 <= r < 32)
                         and r not in vu_ok)
        if extra_i or extra_f:
            bad.append((hex(fn), kind, 'reads', extra_i, extra_f, 'policy compares', na, nf,
                        [hex(scan.first.get((fn, r), 0)) for r in extra_i]))
        for n in notes:
            if n[0] == 'unresolved jump':
                unresolved.add(n[1])
            elif n[0] == 'recursion':
                bad.append((hex(fn), 'recursive call chain not measured', hex(n[1])))
        rows.append((fn, (na, nf), (max([r - 3 for r in ints if 4 <= r <= 11], default=0),
                                    max([r - 11 for r in floats if 12 <= r <= 15], default=0))))
    assert not bad, ('callee register reads outside the compared policy', bad)
    assert unresolved == set(UNRESOLVED_JUMPS), ('unresolved indirect jumps differ from the documented set',
                                                 sorted(hex(x) for x in unresolved))
    saved = {f for (f, r), _ in scan.first.items()
             if (r < 64 and SAVED_READ_MASK_I >> r & 1) or (r >= 64 and SAVED_READ_MASK_F >> (r - 64) & 1)}
    assert saved == set(CALLER_REG_READS), ('caller-saved register reads differ from the documented set',
                                            sorted(hex(f) for f in saved ^ set(CALLER_REG_READS)))
    return rows


# ======================================================================
# Main
# ======================================================================

def jal_targets(elf_ram, start, size):
    out = set()
    for pc in range(start, start + size, 4):
        word = struct.unpack_from('<I', elf_ram, pc)[0]
        if word >> 26 == 3:
            out.add((pc & 0xF0000000) | ((word & 0x3FFFFFF) << 2))
    return out


def branch_pcs(elf_ram, start, size):
    out = set()
    for pc in range(start, start + size, 4):
        word = struct.unpack_from('<I', elf_ram, pc)[0]
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        if op in (4, 5, 20, 21) and not (op == 4 and rs == 0 and rt == 0):
            out.add(pc)
        elif op in (6, 7, 22, 23) or op == 1 or (op == 17 and rs == 8):
            out.add(pc)
    return out


# Conditional-branch outcomes the full sweep cannot take, with the proof.
# EM_TEST_FULL=1 asserts every other outcome of every conditional branch in
# the translated routines is taken, and that these stay untaken.
UNREACHABLE = {
    (0x1A0804, True): 'the negative-index test of 001A06A0: the index is the byte (halfword +0x0E >> 8) '
                      '& 0xFF, computed just before and unchanged, so it lies in 0..255',
    (0x1E3FAC, False): '001E3D90 with record byte +0x0D above 2 (the variant switch falls through): the '
                       'original then uses the row set / pair / phase steps it never set (its caller\'s '
                       'registers); the translation refuses (UNDEFINED) and the fault cases check it',
}
# (0x1E4480, not taken), listed in round 2 as 'only on the UNDEFINED path',
# is reachable: the original reads byte +0x0D again for the sound id after
# its stubbed calls, so a callee that changes it takes the branch (an fx
# case does, and the translation re-reads and skips the call as the original)


def main():
    global NATIVE, ELF, FUNC_RANGES, ELF_IMAGE
    t0 = time.time()
    if AV.ENABLED:
        print('canonical callback mode: all original-instruction comparisons; region arrays disabled', flush=True)
    FUNC_RANGES = tuple((fn, fn + size) for fn, size in sorted(FUNCS.items()))
    ELF = read_elf()
    base = FallEE(ELF)
    ELF_IMAGE = bytes(base.mem[:ELF_END])
    for fn, size in FUNCS.items():
        missing = jal_targets(base.mem, fn, size) - set(CALLEES) - set(FUNCS)
        assert not missing, ('direct call targets without a policy', hex(fn), [hex(m) for m in missing])
        BRANCH_PCS.update(branch_pcs(base.mem, fn, size))
    NATIVE = build_native()
    cases = all_cases()
    # the pointers at +0x4C the cases hand over (zero where a record has none;
    # no case calls those)
    scan_rows = check_register_reads(RegScan(base.mem), {t for c in cases for t in c.get('indirect', ())
                                                         if 0x100000 <= t < 0x275B00})
    only = os.environ.get('EM_AREA01_SYS_ONLY')
    if only:
        cases = [c for c in cases if c['name'].startswith(tuple(only.split(',')))]
    elif not RM.FULL:
        # the round-4 edges of 001E7D20 and 0019B4C0 are the slow ones (a
        # 32 x 32 grid, world probes): full mode only
        cases = [c for c in cases if not c.get('full_only')]
    fxs = fx_cases(cases)
    if RM.FULL or only:
        chosen, fx_chosen = cases, fxs
    else:
        # the default run: the pinned killing cases and a captured smoke
        # case per routine (default_cases), the pinned fx cases
        chosen = default_cases(cases)
        fx_chosen = [v for v in fxs if v['name'] in QUICK_PINNED]
    fx_mode = os.environ.get('EM_AREA01_SYS_FX', '')
    if fx_mode == 'only':
        chosen = []
    elif fx_mode == '0':
        fx_chosen = []
    outcomes, calls, fx_writes = set(), 0, 0
    batch = chosen + fx_chosen
    site_of, ran = {}, []
    for c, (o, _, n, w, sites, extra) in zip(batch, RM.parallel_map(run_case, batch)):
        outcomes |= o
        calls += n
        fx_writes += w
        ran.append((c, extra))
        for pc, v in sites.items():
            seen = site_of.setdefault(pc, {})
            if v not in seen and len(seen) < FX_SITE_VALUES:
                seen[v] = c
    # full mode: the single-load-site variants of every site; both modes:
    # the pinned ones (QUICK_SITE_PINS)
    site_cases = site_variants(site_of) if RM.FULL or only else []
    site_cases += site_pins(cases if fx_mode != '0' else [], {c['name'] for c in site_cases})
    for c, (o, _, n, w, _, extra) in zip(site_cases, RM.parallel_map(run_case, site_cases)):
        outcomes |= o
        calls += n
        fx_writes += w
        ran.append((c, extra))
    store_cases = variants(ran) if RM.FULL else []
    store_cases += pinned_variants(ran, {c['name'] for c in store_cases})
    unmapped = 0
    kinds = {}
    for c, (o, steps, n, w, _, _) in zip(store_cases, RM.parallel_map(run_case, store_cases)):
        outcomes |= o
        calls += n
        unmapped += steps == -1
        k = c['name'].split(' site ')[0] if ' site ' in c['name'] else c['name'].split(' ')[0]
        got = kinds.setdefault(k, [0, 0])
        got[0] += 1
        got[1] += steps == -1
    existing = 0 if only else existing_checks(random.Random(0xE15))
    api = api_checks()
    per = {}
    for fn, size in FUNCS.items():
        pcs = [pc for pc in BRANCH_PCS if fn <= pc < fn + size]
        both = sum(1 for pc in pcs if (pc, True) in outcomes and (pc, False) in outcomes)
        per[fn] = (both, len(pcs))
    ran = sorted({c['fn'] for c in chosen} | {fn for fn in FUNCS if any(fn <= pc < fn + FUNCS[fn] for pc, _ in outcomes)})
    print('branch coverage (both outcomes / conditional branches): ' +
          ', '.join(f'{fn:06X} {per[fn][0]}/{per[fn][1]}' for fn in ran))
    missing = sorted((pc, t) for pc in BRANCH_PCS for t in (True, False) if (pc, t) not in outcomes)
    if os.environ.get('EM_AREA01_SYS_GAPS'):
        for fn in ran:
            miss = [f'{pc:06X}{"T" if t else "F"}' for pc, t in missing if fn <= pc < fn + FUNCS[fn]]
            if miss:
                print(f'  {fn:06X} missing outcomes:', ' '.join(miss))
    if RM.FULL and not only:
        assert set(missing) == set(UNREACHABLE), ('full-mode branch coverage: outcomes not taken beyond the '
                                                  'documented unreachable set',
                                                  [f'{pc:06X}{"T" if t else "F"}' for pc, t in missing
                                                   if (pc, t) not in UNREACHABLE],
                                                  'documented but taken',
                                                  [f'{pc:06X}' for pc, t in UNREACHABLE if (pc, t) not in missing])
        print(f'full-mode branch coverage asserted: every outcome taken except the {len(UNREACHABLE)} '
              'documented unreachable ones')
    print(f'callee register reads measured for {len(scan_rows)} callees: every read register is compared; '
          f'{len(UNRESOLVED_JUMPS)} documented data-driven jump')
    RM.banner(RM.part(len(chosen), len(cases), 'cases'),
              RM.part(len(fx_chosen), len(fxs), 'stub side-effect cases') +
              f' and {len(site_cases)} single-load-site variants ({fx_writes} scripted writes)',
              f'{len(store_cases)} store-site, load-site, stub-result, stub pre-store, aliasing and float-compare '
              f'variants ({unmapped} not comparable: they left the memory the test maps; by kind, run / not '
              'comparable: ' + ', '.join(f'{k} {v[0]}/{v[1]}' for k, v in sorted(kinds.items())) + ')',
              f'{calls} worker calls compared at entry (memory, '
              'registers) and after the last store', f'{len(ran)} routines',
              f'{existing} cases of 00191120 / 001B0C00 / 001FAD70 in their port modules',
              f'{api} fail-stop API checks')
    print(f'test_area01_sys_reference: OK ({time.time() - t0:.1f} s)')


if __name__ == '__main__':
    main()
