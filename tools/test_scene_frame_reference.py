#!/usr/bin/env python3
"""Compare the scene frame core (src/game/em_scene_frame.c, WP-3 step S2) with
the ORIGINAL instructions of 0x1AE040, 0x1AE5E0 and 0x1AE6B0.

The original is executed from the owner's pinned ELF in the base Oracle
(tools/test_point_light_reference.py) via the S1 ClassifierOracle (lhu), extended
here with lb and sltiu. Inside 0x1AE040 the classifier 0x1AE7E0 and both
world-frame variants 0x1AE5E0 / 0x1AE6B0 are EXECUTED as well; the state
dispatch reads its jump table jtbl_0026DD30 from the ELF rodata. Every other
callee is a stub that records (caller, callee, a0..a3, a snapshot of the
coordinator bytes), plants fresh junk in every caller-saved register, and
returns a configured v0 (0022A650, 0020CDC0) or junk.

Native side: the core is built into a dylib with a shim that binds every
EmSceneWorkers entry to a recorder and records the trace hook with a
snapshot of EmSceneState + task bytes + reader values at every call.

Compared per case:
  - ordered (caller, callee, a0..a3). An argument register the call site does
    not set up still holds the planted junk in the original and must be traced
    as 0 by the core (em_scene_frame.h lists those sites); argument registers a
    callee does not use are traced as 0; 001D2610's f12 is traced as its bits;
  - the snapshot at every call (task +8..+0x1F, C4, EF, CE, B8, B9, 3B8D, 3B91,
    3B84, 3B68, D_00810750, D_008101E4, D_00275BE0, D_0028A9A0, D_00275B44), so
    stores are checked against call timing, not only at the end;
  - the complete final state (task bytes, request block, area bytes,
    D_00810730, counters, all scratchpad bytes, flags, input, reader data);
  - the worker arguments the native workers actually received.

Products:
  A. 0x1AE040: +B{0..6} x +C{0,1,2} x BD8{0,1} x D_00282157{0,1} x 3B8D{0,2}
     x (B8,B9){(0,0),(1,0),(2,0),(0,1),(2,1)} x D_0028A9A0{0..3}
     x 0022A650{0,1,2,3} x 0020CDC0{0,1} x classifier input
     {idle, START, SELECT, C5, CE=1, CE=2}, over per-case noise backgrounds,
     plus +B{7,0x80,0xFF} and 0020CDC0{2,0x7FFFFFFF} (a -1 worker result is
     covered in C).
     Stubs sometimes also mutate state ("meddle": canary writes on every
     call; B9/B8 + fade=2 set by the variant's last call; 001AFCF0 clearing
     the request block; D_008102B9 changed between variant calls), identically
     on both sides, to prove the core re-reads what the original re-reads.
     Asserts: +B==0 calls no variant; +B==4 runs the classifier right after
     001C5C50 in the same tick.
     Lead decision Q1 (em_sf_001AE040_q1): equal to the original with SELECT
     withheld from the E74 the classifier reads only.
  B. 0x1AE5E0 / 0x1AE6B0 executed directly: 3B92{0,1,0x80} x 3B91{0,1,2}
     x D_0028A9A0{0,1,2,-1,-32768} x E74{0,0x40,0x100,0x800,0x900,0xFFFF}
     x counters{0, INT_MAX, -1} x 3B84{0,0xFFFF} x meddle.
  C. Fail-stop: every worker reached on representative paths set to NULL
     (fault at that address, trace = the original's prefix, state = the
     original's snapshot at that call), readers NULL, a worker returning -1,
     a latched fault (no call, no store), user == NULL.
  D. The measured traces (docs/ORIGINAL_FRAME_ORDER.md; raw JSON in
     Extermination/build/s87/frame_trace/): for every traced frame the native
     0x1AE040 (variants bound to the native cores) must make exactly the
     original's jal sequence of 0x1AE040 / 001AE5E0 / 001AE6B0.
  E. Oracle extension validation: lb/sltiu on synthetic words, and this
     oracle against tools/test_status_frame_reference.py's independent
     verified oracle over that test's whole product.
"""
import ctypes as C
import hashlib
import itertools
import json
import multiprocessing as mp
import os
from pathlib import Path
import random
import struct
import subprocess
import sys

from test_point_light_reference import RETURN, signed
from test_scene_classify_reference import ClassifierOracle
import test_status_frame_reference as status_ref

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent/'Extermination'
TRACE_DIR = DECOMP/'build/s87/frame_trace'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
TASK = 0x900000
TASK_PTR = 0x70003B6C
E74 = 0x810E74
JTBL = 0x26DD30
# Executed original functions: [start, end).
RANGES = {0x1AE040: (0x1AE040, 0x1AE5D4), 0x1AE5E0: (0x1AE5E0, 0x1AE6A4),
          0x1AE6B0: (0x1AE6B0, 0x1AE7E0), 0x1AE7E0: (0x1AE7E0, 0x1AE900)}
CLOBBER = [1, 2, 3, *range(4, 16), 24, 25]

# Traced argument registers per callee: the callee's declared parameters
# (em_scene_workers.h) plus the call-site extras em_scene_frame.h documents
# (001AD140/001AD010 a0 = D_0028A9A0, 001E0CC0 a0 = 0). Executed callees: 0.
DECL = {
    0x1AFCA0: 0, 0x1AFCF0: 0, 0x1B07C0: 1, 0x1B6990: 0, 0x1D19E0: 0, 0x1C1DC0: 0,
    0x199C50: 0, 0x1AEE40: 1, 0x1FAE70: 1, 0x1C5C50: 0, 0x1D1EF0: 0, 0x18AB00: 0,
    0x18D7B0: 2, 0x18C0D0: 2, 0x1AEE10: 2, 0x1FBC50: 0, 0x1FB9F0: 4, 0x20E060: 0,
    0x1FABB0: 0, 0x119828: 3, 0x1AEDB0: 1, 0x1AD140: 1, 0x1AD010: 1, 0x22A650: 0,
    0x1AF1C0: 0, 0x1AEBA0: 1, 0x1AF150: 0, 0x1D2610: 1, 0x1D1C50: 0, 0x1D2830: 2,
    0x20CDC0: 0, 0x1E0CC0: 1, 0x1D1EA0: 1, 0x1FF030: 1, 0x1FEFE0: 1, 0x1CB590: 4,
    0x15BCF0: 1, 0x1CB5A0: 0, 0x1C1D00: 1, 0x1AFD70: 1, 0x15C160: 0, 0x1F0360: 0,
    0x18B9C0: 1, 0x1AAD00: 0,
    0x1AE7E0: 0, 0x1AE5E0: 0, 0x1AE6B0: 0,
}
READERS = (0x28A9A0, 0x282157, 0x275B44, 0x8102B9)

# ------------------------------------------------------------------ fields
# name -> (original address, size). The task bytes are task+08..task+1F.
SCALARS = [
    ('d810700', 0x810700, 1), ('d810701', 0x810701, 1), ('d810702', 0x810702, 1),
    ('d810750', 0x810750, 4), ('spad3B68', 0x70003B68, 4), ('spad3B84', 0x70003B84, 2),
    ('spad3B8A', 0x70003B8A, 2),
    *[(f'spad3B{x:02X}', 0x70003B00 + x, 1) for x in range(0x8C, 0x94)],
    ('spad3258', 0x70003258, 4), ('spad31F4', 0x700031F4, 4),
    ('d275BD8', 0x275BD8, 1), ('d275BDC', 0x275BDC, 1), ('d275BE0', 0x275BE0, 1),
    ('d8101E4', 0x8101E4, 1), ('d810E74', 0x810E74, 2), ('d810E70', 0x810E70, 2),
    ('d810E50', 0x810E50, 1),
]
EXT = [('fade', 0x28A9A0, 2), ('busy', 0x282157, 1), ('b2b9', 0x8102B9, 1), ('b44', 0x275B44, 4)]
STATE_ORDER = ['req', 'd810700', 'd810701', 'd810702', 'd810730', 'd810750', 'spad3B68',
               'spad3B84', 'spad3B8A', *[f'spad3B{x:02X}' for x in range(0x8C, 0x94)],
               'spad3258', 'spad31F4', 'd275BD8', 'd275BDC', 'd275BE0', 'd8101E4',
               'd810E74', 'd810E70', 'd810E50', 'fault_address', 'fault_code']
EXT_ORDER = ['fade', 'busy', 'b2b9', 'b44']


def all_fields():
    """[(name, address, size)] for every compared byte group."""
    out = [(f'task+{k:02X}', TASK + k, 1) for k in range(8, 0x20)]
    out += [(f'req+{i:02X}', 0x8106B0 + i, 1) for i in range(0x48)]
    out += [(f'd810730+{i:02X}', 0x810730 + i, 1) for i in range(0x20)]
    out += SCALARS + EXT
    return out


FIELDS = all_fields()
FIELD = {name: (address, size) for name, address, size in FIELDS}
SNAP = [*[f'task+{k:02X}' for k in range(8, 0x20)], 'req+14', 'req+3F', 'req+1E', 'req+08',
        'req+09', 'spad3B8D', 'spad3B91', 'spad3B84', 'spad3B68', 'd810750', 'd8101E4',
        'd275BE0', 'fade', 'b44']

# -------------------------------------------------------------- meddle
# Stub side effects applied identically by the oracle stubs and the native
# recorder (SHIM below). They are test devices, not claims about the callees.
MEDDLE_NONE, MEDDLE_CANARY, MEDDLE_B9, MEDDLE_B8, MEDDLE_CLEAR, MEDDLE_PLAYER = range(6)


def apply_meddle(get, put, mode, callee, a0):
    if callee == 0x1CB590:
        put('b44', (a0 + 0x100) & 0xFFFFFFFF)
    if mode == MEDDLE_CANARY:
        put('task+11', (get('task+11') + 1) & 0xFF)
        put('task+0D', (get('task+0D') + 3) & 0xFF)
        put('task+1F', (get('task+1F') + 7) & 0xFF)
        put('req+3F', (get('req+3F') + 5) & 0xFF)
        put('req+14', get('req+14') ^ 0x40)
    elif mode == MEDDLE_B9 and callee == 0x1D1EA0 and a0 == 1:
        put('req+09', 1); put('fade', 2)
    elif mode == MEDDLE_B8 and callee == 0x1D1EA0 and a0 == 1:
        put('req+08', 1); put('req+09', 0); put('fade', 2)
    elif mode == MEDDLE_CLEAR and callee == 0x1AFCF0:
        for i in range(0x48):
            put(f'req+{i:02X}', 0)
        for name in ('spad3B84', 'spad3B8C', 'spad3B8D', 'spad3B8E', 'spad3B8F', 'spad3B91',
                     'spad3B92', 'spad3B93', 'spad3258'):
            put(name, 0)
    elif mode == MEDDLE_PLAYER:
        if callee == 0x1F0360:
            put('b2b9', get('b2b9') ^ 0x5A)
        elif callee == 0x15BCF0:
            put('d810750', (get('d810750') + 0x10) & 0xFFFFFFFF)


# -------------------------------------------------------------- oracle
class FrameOracle(ClassifierOracle):
    """ClassifierOracle + lb (32) + sltiu (11), own run loop with ranges."""

    def __init__(self, elf, results=None, meddle=MEDDLE_NONE, stub_variants=False, status_mimic=False):
        super().__init__(elf)
        self.results = results or {}
        self.meddle = meddle
        self.stub_variants = stub_variants
        self.status_mimic = status_mimic
        self.events = []
        self.junk = {}
        self.seq = 0
        self.plant()

    def plant(self):
        self.seq += 1
        for reg in CLOBBER:
            value = 0xA5000000 | reg << 16 | (self.seq & 0xFFFF)
            self.r[reg] = value
            self.junk[reg] = value

    def plain(self, word):
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        imm = word & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op == 32:  # lb: sign-extended byte
            if rt:
                value = self.load((self.r[rs] + simm) & 0xFFFFFFFF, 1)
                self.r[rt] = (value - 0x100 if value & 0x80 else value) & 0xFFFFFFFF
            return
        if op == 11:  # sltiu: unsigned compare with the sign-extended immediate
            if rt:
                self.r[rt] = int((self.r[rs] & 0xFFFFFFFF) < (simm & 0xFFFFFFFF))
            return
        super().plain(word)
        self.r[0] = 0

    # machine <-> memory
    def put_field(self, name, value):
        address, size = FIELD[name]
        self.save(address, value & ((1 << 8*size) - 1), size)

    def get_field(self, name):
        address, size = FIELD[name]
        return self.load(address, size)

    def load_machine(self, machine):
        for name, _, _ in FIELDS:
            self.put_field(name, machine[name])
        self.save(TASK_PTR, TASK)

    def machine(self):
        return {name: self.get_field(name) for name, _, _ in FIELDS}

    def snap(self):
        return tuple(self.get_field(name) for name in SNAP)

    def stub(self, caller, callee):
        assert callee in DECL, ('unexpected original callee', hex(caller), hex(callee))
        n = DECL[callee]
        if callee == 0x1D2610:
            args = (self.f[12] & 0xFFFFFFFF, 0, 0, 0)
        else:
            args = tuple((0 if self.r[4+i] == self.junk[4+i] else self.r[4+i] & 0xFFFFFFFF)
                         if i < n else 0 for i in range(4))
        self.events.append((caller, callee, args, self.snap()))
        apply_meddle(self.get_field, self.put_field, self.meddle, callee, self.r[4] & 0xFFFFFFFF)
        if self.status_mimic and callee == 0x1FABB0:  # the status oracle's own stub effect
            self.put_field('busy', 0)
        result = self.results.get(callee, None)
        self.plant()
        if result is not None:
            self.r[2] = result & 0xFFFFFFFF

    def function_of(self, pc):
        for entry, (start, end) in RANGES.items():
            if start <= pc < end:
                return entry
        raise AssertionError(('pc outside the executed originals', hex(pc)))

    def run_original(self, entry, q1=False):
        self.r[31] = RETURN
        pc = entry
        restore = None
        for _ in range(20000):
            if pc == RETURN:
                return
            if restore and pc == restore[0]:
                self.save(E74, restore[1], 2)
                restore = None
            fn = self.function_of(pc)
            word = self.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            offset = signed(word & 0xFFFF, 16) * 4
            if op == 3:  # jal
                target = (word & 0x3FFFFFF) << 2
                self.r[31] = pc + 8
                self.plain(self.load(pc + 4))
                executed = target in RANGES and not (self.stub_variants and target in (0x1AE5E0, 0x1AE6B0))
                if executed:
                    self.events.append((fn, target, (0, 0, 0, 0), self.snap()))
                    if target == 0x1AE7E0 and q1:
                        value = self.load(E74, 2)
                        self.save(E74, value & ~0x100 & 0xFFFF, 2)
                        restore = (pc + 8, value)
                    pc = target
                else:
                    self.stub(fn, target)
                    pc += 8
                continue
            assert op != 2, ('unexpected j', hex(pc))
            branch = None
            likely = False
            if op in (4, 5, 20, 21):
                taken = (self.r[rs] & 0xFFFFFFFF) == (self.r[rt] & 0xFFFFFFFF)
                taken = taken if op in (4, 20) else not taken
                likely = op >= 20
            elif op in (6, 7, 22, 23):
                value = signed(self.r[rs])
                taken = value <= 0 if op in (6, 22) else value > 0
                likely = op >= 22
            elif op == 1:
                assert rt in (0, 1, 2, 3), ('regimm', hex(pc))
                value = signed(self.r[rs])
                taken = value < 0 if rt in (0, 2) else value >= 0
                likely = rt >= 2
            elif op == 0 and word & 63 == 8:  # jr
                self.plain(self.load(pc + 4))
                pc = self.r[rs] & 0xFFFFFFFF
                continue
            else:
                try:
                    self.plain(word)
                except AssertionError as error:
                    raise AssertionError(hex(pc), error) from error
                pc += 4
                continue
            if likely and not taken:
                pc += 8
                continue
            branch = pc + 4 + offset if taken else pc + 8
            self.plain(self.load(pc + 4))
            pc = branch
        raise AssertionError('original did not return')


# -------------------------------------------------------------- native shim
SHIM = r'''
#include <stddef.h>
#include <string.h>
#include "game/em_scene_frame.h"

typedef struct { int16_t fade; uint8_t busy; uint8_t b2b9; uint32_t b44; } Ext;
typedef struct {
    uint32_t caller, callee, a[4];
    uint8_t state[sizeof(EmSceneState)];
    uint8_t user[24];
    uint8_t ext[sizeof(Ext)];
} TraceRec;
typedef struct { uint32_t callee, n, a[4]; } WorkRec;

#define MAXREC 160
static EmSceneState S;
static uint8_t U[24];
static Ext X;
static TraceRec TR[MAXREC];
static WorkRec WR[MAXREC];
static int NTR, NWR;
static int R22A650, R20CDC0, MEDDLE, REAL, TRACE_ON = 1;
static uint32_t NULL_ADDR, FAIL_ADDR;
static EmSceneWorkers W;

EmSceneState *shim_state(void) { return &S; }
uint8_t *shim_user(void) { return U; }
Ext *shim_ext(void) { return &X; }
int shim_ntrace(void) { return NTR; }
TraceRec *shim_trace(void) { return TR; }
int shim_nwork(void) { return NWR; }
WorkRec *shim_work(void) { return WR; }

static void trace(void *ctx, uint32_t caller, uint32_t callee, uint32_t a0, uint32_t a1,
                  uint32_t a2, uint32_t a3)
{
    (void)ctx;
    if (NTR < MAXREC) {
        TraceRec *t = &TR[NTR];
        t->caller = caller; t->callee = callee;
        t->a[0] = a0; t->a[1] = a1; t->a[2] = a2; t->a[3] = a3;
        memcpy(t->state, &S, sizeof S);
        memcpy(t->user, U, sizeof U);
        memcpy(t->ext, &X, sizeof X);
    }
    NTR++;
}

#define TASKB(k) U[(k) - 8]
static void meddle(uint32_t callee, uint32_t a0)
{
    if (callee == 0x1CB590)
        X.b44 = a0 + 0x100;
    switch (MEDDLE) {
    case 1:
        TASKB(0x11) += 1; TASKB(0x0D) += 3; TASKB(0x1F) += 7;
        S.req[0x3F] += 5; S.req[0x14] ^= 0x40;
        break;
    case 2:
        if (callee == 0x1D1EA0 && a0 == 1) { S.req[0x09] = 1; X.fade = 2; }
        break;
    case 3:
        if (callee == 0x1D1EA0 && a0 == 1) { S.req[0x08] = 1; S.req[0x09] = 0; X.fade = 2; }
        break;
    case 4:
        if (callee == 0x1AFCF0) {
            memset(S.req, 0, sizeof S.req);
            S.spad3B84 = 0; S.spad3B8C = 0; S.spad3B8D = 0; S.spad3B8E = 0; S.spad3B8F = 0;
            S.spad3B91 = 0; S.spad3B92 = 0; S.spad3B93 = 0; S.spad3258 = 0;
        }
        break;
    case 5:
        if (callee == 0x1F0360) X.b2b9 ^= 0x5A;
        else if (callee == 0x15BCF0) S.d810750 = (int32_t)((uint32_t)S.d810750 + 0x10u);
        break;
    }
}

static int rec(uint32_t callee, uint32_t n, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (NWR < MAXREC) {
        WorkRec *r = &WR[NWR];
        r->callee = callee; r->n = n;
        r->a[0] = a0; r->a[1] = a1; r->a[2] = a2; r->a[3] = a3;
    }
    NWR++;
    if (callee == FAIL_ADDR)
        return -1;
    meddle(callee, a0);
    if (callee == 0x22A650) return R22A650;
    if (callee == 0x20CDC0) return R20CDC0;
    return 0;
}

#define V0(addr) static int w_##addr(void *c) { (void)c; return rec(0x##addr, 0, 0, 0, 0, 0); }
#define V1(addr, T) static int w_##addr(void *c, T a) { (void)c; return rec(0x##addr, 1, (uint32_t)a, 0, 0, 0); }
#define V2(addr, T1, T2) static int w_##addr(void *c, T1 a, T2 b) { (void)c; return rec(0x##addr, 2, (uint32_t)a, (uint32_t)b, 0, 0); }
V0(001AFCA0) V0(001AFCF0) V1(001B07C0, int) V0(001B6990) V0(001D19E0) V0(001C1DC0) V0(00199C50)
V1(001AEE40, int16_t) V1(001FAE70, int) V0(001C5C50) V0(001D1EF0) V0(0018AB00)
V2(0018D7B0, uint32_t, int) V2(0018C0D0, uint32_t, int) V2(001AEE10, int16_t, uint8_t)
V0(001FBC50) V0(0020E060) V0(001FABB0) V1(001AEDB0, uint8_t) V0(001AD140) V0(001AD010)
V0(0022A650) V0(001AF1C0) V1(001AEBA0, int16_t) V0(001AF150) V0(001D1C50) V2(001D2830, int, int)
V0(0020CDC0) V0(001E0CC0) V1(001D1EA0, int) V1(001FF030, uint8_t) V1(001FEFE0, uint8_t)
V1(0015BCF0, uint32_t) V0(001CB5A0) V1(001C1D00, uint32_t) V0(0015C160) V0(001F0360)
V1(0018B9C0, uint32_t) V0(001AAD00)
static int w_001FB9F0(void *c, int a, int b, int d, int e) { (void)c; return rec(0x1FB9F0, 4, (uint32_t)a, (uint32_t)b, (uint32_t)d, (uint32_t)e); }
static int w_00119828(void *c, int a, int b, int d) { (void)c; return rec(0x119828, 3, (uint32_t)a, (uint32_t)b, (uint32_t)d, 0); }
static int w_001D2610(void *c, float f) { uint32_t u; memcpy(&u, &f, 4); (void)c; return rec(0x1D2610, 1, u, 0, 0, 0); }
static int w_001CB590(void *c, uint32_t a, int b, int d, int e) { (void)c; return rec(0x1CB590, 4, a, (uint32_t)b, (uint32_t)d, (uint32_t)e); }
static int walk_001AFD70(void *c, int mode) { (void)c; return rec(0x1AFD70, 1, (uint32_t)mode, 0, 0, 0); }
/* REAL: bound to the native cores; recorded but not meddled, because the
 * oracle executes the original variants instead of stubbing them. */
static int variant(uint32_t callee, int (*core)(EmSceneState *, const EmSceneWorkers *))
{
    if (!REAL)
        return rec(callee, 0, 0, 0, 0, 0);
    if (NWR < MAXREC) {
        memset(&WR[NWR], 0, sizeof WR[NWR]);
        WR[NWR].callee = callee;
    }
    NWR++;
    if (callee == FAIL_ADDR)
        return -1;
    return core(&S, &W);
}
static int w_001AE5E0(void *c) { (void)c; return variant(0x1AE5E0, em_sf_001AE5E0); }
static int w_001AE6B0(void *c) { (void)c; return variant(0x1AE6B0, em_sf_001AE6B0); }
static int16_t r_0028A9A0(void *c) { (void)c; return X.fade; }
static uint8_t r_00282157(void *c) { (void)c; return X.busy; }
static uint32_t r_00275B44(void *c) { (void)c; return X.b44; }
static uint8_t r_008102B9(void *c) { (void)c; return X.b2b9; }

#define FIELDS(F) \
    F(w_001AFCA0, 0x1AFCA0) F(w_001AFCF0, 0x1AFCF0) F(w_001B07C0, 0x1B07C0) F(w_001B6990, 0x1B6990) \
    F(w_001D19E0, 0x1D19E0) F(w_001C1DC0, 0x1C1DC0) F(w_00199C50, 0x199C50) F(w_001AEE40, 0x1AEE40) \
    F(w_001FAE70, 0x1FAE70) F(w_001C5C50, 0x1C5C50) F(w_001D1EF0, 0x1D1EF0) F(w_0018AB00, 0x18AB00) \
    F(w_0018D7B0, 0x18D7B0) F(w_0018C0D0, 0x18C0D0) F(w_001AEE10, 0x1AEE10) F(w_001FBC50, 0x1FBC50) \
    F(w_001FB9F0, 0x1FB9F0) F(w_0020E060, 0x20E060) F(w_001FABB0, 0x1FABB0) F(w_00119828, 0x119828) \
    F(w_001AEDB0, 0x1AEDB0) F(w_001AE5E0, 0x1AE5E0) F(w_001AE6B0, 0x1AE6B0) F(w_001AD140, 0x1AD140) \
    F(w_001AD010, 0x1AD010) F(w_0022A650, 0x22A650) F(w_001AF1C0, 0x1AF1C0) F(w_001AEBA0, 0x1AEBA0) \
    F(w_001AF150, 0x1AF150) F(w_001D2610, 0x1D2610) F(w_001D1C50, 0x1D1C50) F(w_001D2830, 0x1D2830) \
    F(w_0020CDC0, 0x20CDC0) F(w_001E0CC0, 0x1E0CC0) F(w_001D1EA0, 0x1D1EA0) F(w_001FF030, 0x1FF030) \
    F(w_001FEFE0, 0x1FEFE0) F(w_001CB590, 0x1CB590) F(w_0015BCF0, 0x15BCF0) F(w_001CB5A0, 0x1CB5A0) \
    F(w_001C1D00, 0x1C1D00) F(walk_001AFD70, 0x1AFD70) F(w_0015C160, 0x15C160) F(w_001F0360, 0x1F0360) \
    F(w_0018B9C0, 0x18B9C0) F(w_001AAD00, 0x1AAD00) \
    F(r_0028A9A0, 0x28A9A0) F(r_00282157, 0x282157) F(r_00275B44, 0x275B44) F(r_008102B9, 0x8102B9)

void shim_config(int r22A650, int r20CDC0, int meddle_mode, int real, uint32_t null_addr,
                 uint32_t fail_addr, int trace_on)
{
    R22A650 = r22A650; R20CDC0 = r20CDC0; MEDDLE = meddle_mode; REAL = real;
    NULL_ADDR = null_addr; FAIL_ADDR = fail_addr; TRACE_ON = trace_on;
    memset(&W, 0, sizeof W);
#define ASSIGN(f, addr) if (NULL_ADDR != (addr)) W.f = f;
    FIELDS(ASSIGN)
#undef ASSIGN
    W.ctx = NULL;
    W.trace = TRACE_ON ? trace : NULL;
}

int shim_run(int which, int *withheld)
{
    NTR = NWR = 0;
    *withheld = -1;
    switch (which) {
    case 0: return em_sf_001AE040(&S, U, &W);
    case 1: return em_sf_001AE040_q1(&S, U, &W, withheld);
    case 2: return em_sf_001AE5E0(&S, &W);
    case 3: return em_sf_001AE6B0(&S, &W);
    case 4: return em_sf_001AE040(&S, NULL, &W);
    case 5: return em_sf_001AE040(&S, U, NULL);
    }
    return -2;
}

int shim_layout(uint32_t *out)
{
    int i = 0;
    out[i++] = sizeof(EmSceneState); out[i++] = sizeof(TraceRec);
    out[i++] = offsetof(TraceRec, state); out[i++] = offsetof(TraceRec, user);
    out[i++] = offsetof(TraceRec, ext); out[i++] = sizeof(WorkRec); out[i++] = sizeof(Ext);
#define O(f) out[i++] = offsetof(EmSceneState, f);
    O(req) O(d810700) O(d810701) O(d810702) O(d810730) O(d810750) O(spad3B68) O(spad3B84)
    O(spad3B8A) O(spad3B8C) O(spad3B8D) O(spad3B8E) O(spad3B8F) O(spad3B90) O(spad3B91)
    O(spad3B92) O(spad3B93) O(spad3258) O(spad31F4) O(d275BD8) O(d275BDC) O(d275BE0)
    O(d8101E4) O(d810E74) O(d810E70) O(d810E50) O(fault.address) O(fault.code)
#undef O
    out[i++] = offsetof(Ext, fade); out[i++] = offsetof(Ext, busy);
    out[i++] = offsetof(Ext, b2b9); out[i++] = offsetof(Ext, b44);
    return i;
}
'''


class Native:
    def __init__(self, library):
        L = self.lib = C.CDLL(str(library))
        L.shim_config.argtypes = [C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint32, C.c_uint32, C.c_int]
        L.shim_run.argtypes = [C.c_int, C.POINTER(C.c_int)]
        for name in ('shim_state', 'shim_user', 'shim_ext', 'shim_trace', 'shim_work'):
            getattr(L, name).restype = C.c_void_p
        layout = (C.c_uint32 * 64)()
        count = L.shim_layout(layout)
        values = list(layout)[:count]
        (self.ssize, self.trsize, self.tr_state, self.tr_user, self.tr_ext, self.wrsize,
         self.esize) = values[:7]
        state_offsets = dict(zip(STATE_ORDER, values[7:7 + len(STATE_ORDER)]))
        ext_offsets = dict(zip(EXT_ORDER, values[7 + len(STATE_ORDER):]))
        self.state_ptr, self.user_ptr, self.ext_ptr = L.shim_state(), L.shim_user(), L.shim_ext()
        # name -> (area, offset, size, signed)
        self.spec = {}
        for k in range(8, 0x20):
            self.spec[f'task+{k:02X}'] = ('u', k - 8, 1)
        for i in range(0x48):
            self.spec[f'req+{i:02X}'] = ('s', state_offsets['req'] + i, 1)
        for i in range(0x20):
            self.spec[f'd810730+{i:02X}'] = ('s', state_offsets['d810730'] + i, 1)
        for name, _, size in SCALARS:
            self.spec[name] = ('s', state_offsets[name], size)
        for name, _, size in EXT:
            self.spec[name] = ('x', ext_offsets[name], size)
        self.fault_offsets = (state_offsets['fault_address'], state_offsets['fault_code'])

    @staticmethod
    def _get(buffers, spec):
        area, offset, size = spec
        return int.from_bytes(buffers[area][offset:offset + size], 'little')

    def load(self, machine):
        buffers = {'s': bytearray(self.ssize), 'u': bytearray(24), 'x': bytearray(self.esize)}
        for name, (area, offset, size) in self.spec.items():
            buffers[area][offset:offset + size] = (machine[name] & ((1 << 8*size) - 1)).to_bytes(size, 'little')
        C.memmove(self.state_ptr, bytes(buffers['s']), self.ssize)
        C.memmove(self.user_ptr, bytes(buffers['u']), 24)
        C.memmove(self.ext_ptr, bytes(buffers['x']), self.esize)

    def buffers(self):
        return {'s': C.string_at(self.state_ptr, self.ssize), 'u': C.string_at(self.user_ptr, 24),
                'x': C.string_at(self.ext_ptr, self.esize)}

    def machine(self):
        b = self.buffers()
        return {name: self._get(b, spec) for name, spec in self.spec.items()}

    def fault(self):
        s = self.buffers()['s']
        address = int.from_bytes(s[self.fault_offsets[0]:self.fault_offsets[0] + 4], 'little')
        code = int.from_bytes(s[self.fault_offsets[1]:self.fault_offsets[1] + 4], 'little', signed=True)
        return address, code

    def run(self, which, results, meddle, real=1, null_addr=0, fail_addr=0, trace_on=1):
        self.lib.shim_config(results.get(0x22A650, 0), results.get(0x20CDC0, 0), meddle, real,
                             null_addr, fail_addr, trace_on)
        withheld = C.c_int(-1)
        ret = self.lib.shim_run(which, C.byref(withheld))
        count = self.lib.shim_ntrace()
        assert count <= 160
        raw = C.string_at(self.lib.shim_trace(), self.trsize * count)
        events = []
        for i in range(count):
            rec = raw[i*self.trsize:(i+1)*self.trsize]
            caller, callee, *args = struct.unpack_from('<6I', rec, 0)
            b = {'s': rec[self.tr_state:self.tr_state + self.ssize],
                 'u': rec[self.tr_user:self.tr_user + 24],
                 'x': rec[self.tr_ext:self.tr_ext + self.esize]}
            events.append((caller, callee, tuple(args), tuple(self._get(b, self.spec[n]) for n in SNAP)))
        nwork = self.lib.shim_nwork()
        rawwork = C.string_at(self.lib.shim_work(), self.wrsize * nwork)
        work = [struct.unpack_from('<6I', rawwork, i*self.wrsize) for i in range(nwork)]
        return ret, withheld.value, events, work


# ------------------------------------------------------------ comparison
def check_work(events, work, context):
    """The native workers received exactly the traced declared arguments."""
    calls = [e for e in events if e[1] != 0x1AE7E0]
    assert len(calls) == len(work), ('trace/worker count', context, len(calls), len(work))
    for (caller, callee, args, _), (wcallee, n, *wargs) in zip(calls, work):
        assert callee == wcallee, ('trace/worker callee', context, hex(callee), hex(wcallee))
        for i in range(n):
            # typed parameters: compare as the declared width sees them
            assert wargs[i] == args[i], ('worker arg', context, hex(callee), i, wargs, args)


def describe(event):
    caller, callee, args, _ = event
    return f'{caller:06X}->{callee:06X}{tuple(hex(a) for a in args)}'


def compare(expected, got, context):
    if len(expected) != len(got) or any(e[:3] != g[:3] for e, g in zip(expected, got)):
        raise AssertionError(('call order/args', context, [describe(e) for e in expected],
                              [describe(g) for g in got]))
    for index, (e, g) in enumerate(zip(expected, got)):
        if e[3] != g[3]:
            diff = {n: (hex(a), hex(b)) for n, a, b in zip(SNAP, e[3], g[3]) if a != b}
            raise AssertionError(('snapshot at call', context, index, describe(e), diff))


def compare_machine(expected, got, context):
    diff = {n: (hex(expected[n]), hex(got[n])) for n in expected if expected[n] != got[n]}
    assert not diff, ('final state', context, diff)


# ------------------------------------------------------------- products
CLASSIFIER_INPUTS = {
    'idle':   {},
    'start':  {'d810E74': 0x800},
    'select': {'d810E74': 0x100},
    'status': {'req+15': 1},
    'ce1':    {'req+1E': 1},
    'ce2':    {'req+1E': 2},
}
B8B9 = [(0, 0), (1, 0), (2, 0), (0, 1), (2, 1)]


def background(rng):
    m = {}
    for name, _, size in FIELDS:
        m[name] = rng.randrange(1 << (8*size))
    m['d810750'] = rng.choice((rng.randrange(1 << 32), 0x7FFFFFFF, 0xFFFFFFFF, 0))
    m['spad3B68'] = rng.choice((rng.randrange(1 << 32), 0x7FFFFFFF, 0xFFFFFFFF, 0))
    m['spad3B84'] = rng.choice((rng.randrange(1 << 16), 0xFFFF))
    # classifier inputs neutral unless the case sets them
    for key in ('req+00', 'req+03', 'req+15', 'req+1E', 'req+08', 'req+09'):
        m[key] = 0
    m['d810E74'] = 0
    m['d810E50'] = 4
    return m


def frame_cases():
    product = itertools.product(range(7), (0, 1, 2), (0, 1), (0, 1), (0, 2), B8B9, (0, 1, 2, 3),
                                (0, 1, 2, 3), (0, 1), sorted(CLASSIFIER_INPUTS))
    for values in product:
        yield values
    # out-of-range +B and extra 0020CDC0 results (tested only != 0)
    for b in (7, 0x80, 0xFF):
        for cls in sorted(CLASSIFIER_INPUTS):
            yield (b, 0, 0, 0, 0, (0, 0), 0, 1, 1, cls)
    for r in (2, 0x7FFFFFFF):
        for c in (0, 1):
            yield (3, c, 0, 0, 0, (0, 0), 0, 0, r, 'idle')


def frame_machine(index, values):
    b, c, bd8, busy, sel, (b8, b9), fade, r22, r20, cls = values
    rng = random.Random(0x1AE040 * 1000003 + index)
    m = background(rng)
    m['task+0B'], m['task+0C'] = b, c
    m['d275BD8'], m['busy'], m['spad3B8D'] = bd8, busy, sel
    m['req+08'], m['req+09'], m['fade'] = b8, b9, fade
    m.update(CLASSIFIER_INPUTS[cls])
    meddle = rng.choice((MEDDLE_NONE, MEDDLE_NONE, MEDDLE_NONE, MEDDLE_CANARY, MEDDLE_B9,
                         MEDDLE_B8, MEDDLE_CLEAR, MEDDLE_PLAYER))
    return m, {0x22A650: r22, 0x20CDC0: r20}, meddle


def run_frame_case(elf, native, index, values, q1=False):
    m, results, meddle = frame_machine(index, values)
    context = (index, values, 'meddle', meddle, 'q1', q1)
    o = FrameOracle(elf, results, meddle)
    o.load_machine(m)
    o.run_original(0x1AE040, q1=q1)
    native.load(m)
    ret, withheld, events, work = native.run(1 if q1 else 0, results, meddle)
    assert ret == 0 and native.fault() == (0, 0), ('native returned', context, ret, native.fault())
    compare(o.events, events, context)
    check_work(events, work, context)
    compare_machine(o.machine(), native.machine(), context)
    b = m['task+0B']
    callees = [e[1] for e in o.events]
    if b == 0:
        assert 0x1AE5E0 not in callees and 0x1AE6B0 not in callees, ('state 0 ran a variant', context)
    if b == 4:
        k = callees.index(0x1C5C50)
        assert callees[k + 1] == 0x1AE7E0, ('state 4 did not fall through', context)
    if q1:
        classified = 0x1AE7E0 in callees
        assert withheld == int(classified and bool(m['d810E74'] & 0x100)), ('Q1 flag', context, withheld)
    classifier_ran = 0x1AE7E0 in callees
    variant = next((c for c in callees if c in (0x1AE5E0, 0x1AE6B0)), None)
    return {'classifier_ran': classifier_ran, 'variant': variant, 'meddle': meddle,
            'calls': len(o.events)}


def variant_cases():
    return itertools.product((0x1AE5E0, 0x1AE6B0), (0, 1, 0x80), (0, 1, 2),
                             (0, 1, 2, 0xFFFF, 0x8000), (0, 0x40, 0x100, 0x800, 0x900, 0xFFFF),
                             ((0, 0), (0x7FFFFFFF, 0xFFFFFFFF), (0xFFFFFFFF, 0x7FFFFFFF)),
                             (0, 0xFFFF), (MEDDLE_NONE, MEDDLE_CANARY, MEDDLE_PLAYER))


def run_variant_case(elf, native, index, values):
    entry, s92, s91, fade, e74, (c750, c68), s84, meddle = values
    rng = random.Random(0x1AE5E0 * 1000003 + index)
    m = background(rng)
    m.update({'spad3B92': s92, 'spad3B91': s91, 'fade': fade, 'd810E74': e74,
              'd810750': c750, 'spad3B68': c68, 'spad3B84': s84})
    context = (index, [hex(v) if isinstance(v, int) else v for v in values])
    o = FrameOracle(elf, {}, meddle)
    o.load_machine(m)
    o.run_original(entry)
    native.load(m)
    ret, _, events, work = native.run(2 if entry == 0x1AE5E0 else 3, {}, meddle)
    assert ret == 0 and native.fault() == (0, 0), ('native returned', context, ret)
    compare(o.events, events, context)
    check_work(events, work, context)
    compare_machine(o.machine(), native.machine(), context)
    return {'promoted_3B91': m['spad3B91'] == 1 and o.get_field('spad3B91') == 2,
            'b84_incremented': o.get_field('spad3B84') != m['spad3B84']}


# ------------------------------------------------------- worker processes
_ELF = None
_NATIVE = None


def _init(library):
    global _ELF, _NATIVE
    _ELF = (DECOMP/'config/SCUS_971.12').read_bytes()
    _NATIVE = Native(library)


def _frame_chunk(chunk):
    stats = {'cases': 0, 'q1_cases': 0, 'classifier_ran': 0, 'variant_5E0': 0, 'variant_6B0': 0,
             'meddle': [0]*6, 'max_calls': 0}
    for index, values in chunk:
        info = run_frame_case(_ELF, _NATIVE, index, values)
        stats['cases'] += 1
        stats['classifier_ran'] += info['classifier_ran']
        stats['variant_5E0'] += info['variant'] == 0x1AE5E0
        stats['variant_6B0'] += info['variant'] == 0x1AE6B0
        stats['meddle'][info['meddle']] += 1
        stats['max_calls'] = max(stats['max_calls'], info['calls'])
        if values[0] in (1, 4) and values[-1] in ('select', 'idle', 'start'):
            run_frame_case(_ELF, _NATIVE, index, values, q1=True)
            stats['q1_cases'] += 1
    return stats


def _variant_chunk(chunk):
    stats = {'cases': 0, 'promoted_3B91': 0, 'b84_incremented': 0}
    for index, values in chunk:
        info = run_variant_case(_ELF, _NATIVE, index, values)
        stats['cases'] += 1
        stats['promoted_3B91'] += info['promoted_3B91']
        stats['b84_incremented'] += info['b84_incremented']
    return stats


def merge(total, part):
    for key, value in part.items():
        if isinstance(value, list):
            total[key] = [a + b for a, b in zip(total.get(key, [0]*len(value)), value)]
        elif key.startswith('max_'):
            total[key] = max(total.get(key, 0), value)
        else:
            total[key] = total.get(key, 0) + value
    return total


def chunks(items, size):
    batch = []
    for item in items:
        batch.append(item)
        if len(batch) == size:
            yield batch
            batch = []
    if batch:
        yield batch


# ------------------------------------------------------------ fail-stop
def fail_stop(elf, native):
    checks = 0
    # Representative paths: every state and every classifier arm.
    paths = [
        (0, 0, 0, 0, 0, (0, 0), 0, 0, 0, 'idle'),
        (4, 0, 0, 0, 0, (0, 0), 0, 0, 0, 'idle'),
        (4, 0, 0, 0, 2, (2, 0), 2, 0, 0, 'idle'),   # 001AE6B0 then 001AD010
        (1, 0, 0, 0, 0, (0, 1), 2, 0, 0, 'idle'),   # 001AE5E0 then 001AD140
        (1, 0, 0, 0, 0, (0, 0), 0, 0, 0, 'select'),
        (1, 0, 0, 0, 0, (0, 0), 0, 0, 0, 'status'),
        (1, 0, 0, 0, 0, (0, 0), 0, 0, 0, 'ce1'),
        (2, 0, 0, 0, 0, (0, 0), 0, 1, 0, 'idle'),
        (2, 0, 0, 0, 0, (0, 0), 0, 2, 0, 'idle'),
        (2, 0, 0, 0, 0, (0, 0), 0, 3, 0, 'idle'),
        (3, 0, 0, 0, 0, (0, 0), 0, 0, 0, 'idle'),
        (3, 1, 0, 0, 0, (0, 0), 0, 0, 1, 'idle'),
        (5, 0, 0, 0, 0, (0, 0), 0, 0, 0, 'idle'),
        (6, 0, 0, 0, 0, (0, 0), 0, 0, 0, 'ce2'),
        (6, 0, 0, 0, 0, (0, 0), 0, 0, 0, 'ce1'),
        (6, 1, 0, 0, 0, (0, 0), 0, 0, 0, 'ce1'),
    ]
    for index, values in enumerate(paths):
        m, results, _ = frame_machine(index, values)
        o = FrameOracle(elf, results, MEDDLE_NONE)
        o.load_machine(m)
        o.run_original(0x1AE040)
        full = o.events
        # NULL worker at each reached call (the classifier is not a worker).
        for k, event in enumerate(full):
            callee = event[1]
            if callee == 0x1AE7E0:
                continue
            native.load(m)
            ret, _, events, _ = native.run(0, results, MEDDLE_NONE, null_addr=callee)
            first = next(i for i, e in enumerate(full) if e[1] == callee)
            assert ret == -1 and native.fault() == (callee, 1), ('NULL worker', values, hex(callee), ret, native.fault())
            compare(full[:first], events, ('NULL prefix', values, hex(callee)))
            # the state at the fault equals the original's state at that call
            got = native.machine()
            assert tuple(got[n] for n in SNAP) == full[first][3], ('state at NULL fault', values, hex(callee))
            # latched: a second tick does nothing
            before = native.machine()
            ret2, _, events2, work2 = native.run(0, results, MEDDLE_NONE, null_addr=callee)
            assert ret2 == -1 and not events2 and not work2 and native.machine() == before
            checks += 2
            # a worker returning -1 faults after its (traced) call; nothing follows
            native.load(m)
            ret, _, events, work = native.run(0, results, MEDDLE_NONE, fail_addr=callee)
            assert ret == -1 and native.fault() == (callee, 2), ('failing worker', values, hex(callee), native.fault())
            assert [e[1] for e in events] == [e[1] for e in full[:first + 1]], ('fail prefix', values, hex(callee))
            checks += 1
        # NULL readers: fault at the reader's address iff the path reads it.
        for reader in READERS:
            native.load(m)
            ret, _, events, _ = native.run(0, results, MEDDLE_NONE, null_addr=reader)
            full_native = native_full = None
            if ret == -1:
                assert native.fault() == (reader, 1), ('NULL reader', values, hex(reader), native.fault())
                assert [e[:3] for e in events] == [e[:3] for e in full[:len(events)]]
            else:
                native_full = native.machine()
                full_native = o.machine()
                compare_machine(full_native, native_full, ('reader unused', values, hex(reader)))
            checks += 1
    # user == NULL and workers == NULL
    m, results, _ = frame_machine(0, paths[1])
    native.load(m)
    ret, _, events, _ = native.run(4, results, MEDDLE_NONE)
    assert ret == -1 and native.fault() == (0x1AE040, 4) and not events
    native.load(m)
    ret, _, events, _ = native.run(5, results, MEDDLE_NONE)
    assert ret == -1 and native.fault()[1] == 1 and not events
    # A NULL trace hook changes nothing.
    m, results, _ = frame_machine(3, paths[3])
    native.load(m)
    ret, _, events, _ = native.run(0, results, MEDDLE_NONE, trace_on=0)
    silent = native.machine()
    native.load(m)
    native.run(0, results, MEDDLE_NONE)
    assert ret == 0 and not events and silent == native.machine()
    return checks + 3


# ------------------------------------------------ measured frame traces
def measured_traces(native):
    names = {0x1AE040: 'anim_frame_top_b', 0x1AE5E0: 'func_001AE5E0', 0x1AE6B0: 'func_001AE6B0'}
    frames = 0
    per_file = {}
    for path in sorted(TRACE_DIR.glob('*.json')):
        if path.name in ('cg.json', 'frame_order_summary.json'):
            continue
        data = json.loads(path.read_text())
        ok = 0
        for frame in data['frames']:
            events = frame['events']
            entry = next((e for e in events if e.get('entry') == 'anim_frame_top_b'), None)
            if entry is None:
                continue
            original = [(e['fn'], e['target']) for e in events
                        if e.get('op') == 'jal' and e.get('fn') in names.values()]
            ret_event = next((e for e in events if 'classifier_ret' in e), None)
            assert ret_event is None or ret_event['classifier_ret'] == 0, (path.name, ret_event)
            m = {name: 0 for name, _, _ in FIELDS}
            m['d810E50'] = 4
            m['task+08'], m['task+09'], m['task+0A'] = entry['b8'], entry['b9'], entry['bA']
            m['task+0B'], m['task+0C'] = entry['state_B'], entry['sub_C']
            m['d275BD8'], m['spad3B8D'] = entry['busy_275BD8'], entry['sel_3B8D']
            assert frame['selector_3B8D'] == entry['sel_3B8D']
            page = int(any(t == 'func_001E0CC0' for _, t in original))
            native.load(m)
            ret, _, got, _ = native.run(0, {0x20CDC0: page}, MEDDLE_NONE)
            assert ret == 0
            mine = [(names[c], f'func_{t:08X}') for c, t, _, _ in got]
            assert mine == original, ('measured order', path.name, frame['counter'], mine, original)
            ok += 1
        per_file[path.stem] = ok
        frames += ok
    assert frames >= 17, per_file
    return frames, per_file


# --------------------------------------------- oracle extension checks
def validate_extensions(elf):
    checks = 0
    for value in (0x00, 0x01, 0x7F, 0x80, 0xFF):
        for offset in (-1, 0, 3):
            o = FrameOracle(elf)
            o.save(0x500100 + offset, value, 1)
            o.save(0x500100 + offset + 1, 0xA5, 1)
            o.r[4] = 0x500100
            o.plain((32 << 26) | (4 << 21) | (2 << 16) | (offset & 0xFFFF))
            assert o.r[2] == (value - 0x100 if value & 0x80 else value) & 0xFFFFFFFF
            checks += 1
    o = FrameOracle(elf)
    for rs_value, imm, expected in ((6, 7, 1), (7, 7, 0), (0xFFFFFFFF, 7, 0), (5, 0xFFFF, 1),
                                    (0xFFFFFFFE, 0xFFFF, 1), (0xFFFFFFFF, 0xFFFF, 0), (0, 0, 0)):
        o.r[4] = rs_value
        o.plain((11 << 26) | (4 << 21) | (2 << 16) | imm)
        assert o.r[2] == expected, (hex(rs_value), hex(imm), o.r[2])
        checks += 1
    return checks


def cross_check_status_oracle(elf):
    """This oracle vs the verified status oracle over its whole product."""
    event_of = {callee: event for callee, (event, _) in status_ref.CALLS.items()}
    cases = 0
    for phase, step, flag, control, recovery, busy, result in itertools.product(
            (1, 3, 5), (0, 1, 2), (0, 17), (0, 1), (0, 70, 80), (0, 1), (0, 1)):
        initial = status_ref.Status(phase, step, flag, control, recovery, busy)
        expected, expected_events = status_ref.Original(elf, initial, result).run()
        o = FrameOracle(elf, {0x20CDC0: result}, status_mimic=True)
        m = {name: 0 for name, _, _ in FIELDS}
        m['d810E50'] = 4
        m['req+15'] = 1  # classifier -> 2, as the status oracle's forced return
        m.update({'task+0B': phase, 'task+0C': step, 'task+11': flag, 'req+14': control,
                  'req+3F': recovery, 'busy': busy})
        o.load_machine(m)
        o.run_original(0x1AE040)
        mine = []
        for _, callee, args, _ in o.events:
            if callee == 0x1AE7E0:
                continue
            if callee == 0x20CDC0:
                mine.append(14)
            elif callee == 0x119828:
                mine.append(3 + args[0])
            else:
                mine.append(event_of[callee])
        got = (o.get_field('task+0B'), o.get_field('task+0C'), o.get_field('task+11'),
               o.get_field('req+14'), o.get_field('req+3F'), o.get_field('busy'))
        assert got == tuple(bytes(expected)), ('status oracle state', phase, step, got, bytes(expected))
        assert mine == expected_events, ('status oracle events', phase, step, mine, expected_events)
        cases += 1
    return cases


def jump_table(elf):
    table = [int.from_bytes(elf[JTBL - 0x100000 + 0x300 + 4*i:][:4], 'little') for i in range(7)]
    assert all(0x1AE040 <= t < 0x1AE5D4 for t in table), [hex(t) for t in table]
    assert table[1] == 0x1AE154 and table[4] == 0x1AE0E4, [hex(t) for t in table]
    return table


# ------------------------------------------------------------------- main
def main():
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    out = ROOT/'build/scene_frame_reference'
    out.mkdir(parents=True, exist_ok=True)
    ext = 'dylib' if sys.platform == 'darwin' else 'so'
    link = '-dynamiclib' if sys.platform == 'darwin' else '-shared'
    (out/'shim.c').write_text(SHIM)
    library = out/f'scene_frame.{ext}'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', link, '-Isrc',
                    'src/game/em_scene_frame.c', 'src/game/em_scene_classify.c',
                    'src/game/em_status_frame.c', str(out/'shim.c'), '-o', str(library)],
                   cwd=ROOT, check=True)
    native = Native(library)

    report = {'status': 'PASS', 'original_entries': ['0x1AE040', '0x1AE5E0', '0x1AE6B0'],
              'executed_inside_0x1AE040': ['0x1AE7E0', '0x1AE5E0', '0x1AE6B0'],
              'jump_table_0x26DD30': [hex(t) for t in jump_table(elf)],
              'oracle_lb_sltiu_checks': validate_extensions(elf),
              'oracle_vs_status_oracle_cases': cross_check_status_oracle(elf)}

    workers = max(1, min(os.cpu_count() or 1, 10))
    frame_items = list(enumerate(frame_cases()))
    variant_items = list(enumerate(variant_cases()))
    ctx = mp.get_context('spawn')
    with ctx.Pool(workers, initializer=_init, initargs=(str(library),)) as pool:
        frame_stats = {}
        for part in pool.imap_unordered(_frame_chunk, chunks(frame_items, 500)):
            merge(frame_stats, part)
        variant_stats = {}
        for part in pool.imap_unordered(_variant_chunk, chunks(variant_items, 200)):
            merge(variant_stats, part)
    assert frame_stats['cases'] == len(frame_items)
    assert variant_stats['cases'] == len(variant_items)
    assert all(frame_stats['meddle']), frame_stats['meddle']
    assert frame_stats['variant_5E0'] and frame_stats['variant_6B0'] and frame_stats['q1_cases']
    assert variant_stats['promoted_3B91'] and variant_stats['b84_incremented']
    report['frame_cases_0x1AE040'] = frame_stats
    report['variant_cases'] = variant_stats
    report['fail_stop_checks'] = fail_stop(elf, native)
    frames, per_file = measured_traces(native)
    report['measured_frames_matching_ORIGINAL_FRAME_ORDER'] = frames
    report['measured_frames_per_trace'] = per_file
    report['original_elf_sha256'] = ELF_SHA256
    (out/'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
