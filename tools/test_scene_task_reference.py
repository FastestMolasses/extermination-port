#!/usr/bin/env python3
"""Compare the scene task chain cores (src/game/em_scene_task.c, WP-3 step S3)
with the original ELF instructions.

Executed originals (SCENE_COORDINATOR_DESIGN.md sections 2.2 and 2.6):
  001ACEC0, 001AD250, 001AD360, 001ADF50, 001AD4E0, 001ADF00 (the task chain,
  entered at 001ACEC0 and at each sub-machine), 001AD010, 001AD140, 001AFCF0,
  and the memset 00121A28 that 001AFCF0 calls.
Every other callee is a recorded stub: its a0..a3, the owned state at the jal
(after the delay slot), and a configured v0 (001AD1A0, 001AD230, 0021B550;
garbage for the rest). Caller-saved registers are clobbered at every stub.

Owned state is the EmSceneState bytes plus the task record +8..+0x1F, laid out
as the "snapshot" below at their original addresses (task record at TASK,
reached through 0x70003B6C). Loads and stores of the original are checked: a
store may only hit the owned state, a store-worker address (D_00821058,
D_00275C78, D_00810D38) or the stack; a load may only hit the ELF image, the
owned state, the slot pointer, the readers (D_0028A9A0, D_00282157) or the
stack. The store-worker stores and the reader loads are events, in order with
the calls.

For every case the native core must produce: the same ordered events (callee,
declared arguments, owned state at that moment), the same trace (caller,
callee, a0..a3 the call site sets up; cores and 00121A28 included), the same
final owned state and the same return value. Cases run with and without a
"perturbing" stub that adds 1 to every owned byte after each call, which
proves the native core reloads state after calls exactly where the original
does, and with 0021B550 clearing BD8 (its role in 001ADF50).

Fail-stop is checked against the original too: for every event k, making that
worker fail (or be NULL) must leave exactly the original state at event k,
latch the fault at that address, and make no further call. 001AD010 with B6 ==
0xFF and B5 >= 0x20 (a read past the owned D_00810730 table) must fault at the
original's load, with the original's state at that load.

The oracle ISA extensions (sltiu/sltu, lb, lhu, dsll, dsll32, pcpyh, pcpyld,
sd) are validated first: lb/lhu/sltu/sltiu on synthetic values, and the whole
set by executing the original memset 00121A28 over alignments, lengths and fill
values against Python's result.

Design coverage (section 6, row S3): all +8/+9/+A arms (plus out-of-range
values), B5..B8, 3B93 {0,1,2,3,0xFF}, B6 = 0xFF with a D_00810730 table whose
bytes have bit 7 set, 0021B550 {0,1,4}, BD8, fade (D_0028A9A0) and
+0x18/E74 for the game-over countdown.
"""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import random
import subprocess
import sys

from test_point_light_reference import Oracle, RETURN, STACK, signed

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent/'Extermination'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
TASK = 0x900000
SLOT = 0x70003B6C
M64 = (1 << 64) - 1

# Executed original functions: start -> size (splat .s headers).
FUNCS = {0x1ACEC0: 0x148, 0x1AD010: 0x12C, 0x1AD140: 0x60, 0x1AD250: 0x108,
         0x1AD360: 0x16C, 0x1AD4E0: 0x258, 0x1ADF00: 0x4C, 0x1ADF50: 0xEC,
         0x1AFCF0: 0x78, 0x121A28: 0xC0}
ENTRIES = [0x1ACEC0, 0x1AD250, 0x1AD360, 0x1ADF50, 0x1AD4E0, 0x1ADF00, 0x1AD010,
           0x1AD140, 0x1AFCF0]
RETURNS_VALUE = {0x1AD360, 0x1ADF50}

# Owned state layout (address, size); the native shim uses the same order.
LAYOUT = [(TASK+8, 24), (0x8106B0, 0x48), (0x810700, 3), (0x810730, 0x20),
          (0x810750, 4), (0x70003B68, 4), (0x70003B84, 2), (0x70003B8A, 2),
          (0x70003B8C, 8), (0x70003258, 4), (0x700031F4, 4), (0x275BD8, 1),
          (0x275BDC, 1), (0x275BE0, 1), (0x8101E4, 1), (0x810E74, 2),
          (0x810E70, 2), (0x810E50, 1)]
SNAP = sum(size for _, size in LAYOUT)
OFFSET = {}
_o = 0
for _a, _n in LAYOUT:
    for _i in range(_n):
        OFFSET[_a+_i] = _o+_i
    _o += _n
OWNED = set(OFFSET)

READERS = {0x28A9A0: 2, 0x282157: 1}
STORES = {0x821058: 1, 0x275C78: 1, 0x810D38: 4}
OOB = range(0x810750, 0x810830)   # D_00810730[0x20..0xFF]
RESULT_CALLEES = (0x1AD1A0, 0x1AD230, 0x21B550)
# Argument registers each callee declares (decomp C); compared on events and trace.
ARGS = {0x1D2830: 2, 0x1AEDB0: 1, 0x1AED80: 1, 0x1FF080: 2, 0x1AEE10: 2, 0x1FA790: 2,
        0x1ABF90: 4, 0x1AEDE0: 2, 0x1AEBA0: 1, 0x1AB790: 1, 0x121A28: 3}
# Trace-only arguments: registers the call site sets up for a void callee.
TRACE_ARGS = {(0x1AD010, 0x1FBC50): 2}
KIND = {'call': 1, 'store': 2, 'read': 3}
FAULT_NULL, FAULT_FAILED, FAULT_INDEX = 1, 2, 4
CALLER_SAVED = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25]


def put(snap, address, value, size=1):
    for i in range(size):
        snap[OFFSET[address+i]] = value >> (8*i) & 0xFF


def get(snap, address, size=1):
    return sum(snap[OFFSET[address+i]] << (8*i) for i in range(size))


class TaskOracle(Oracle):
    """Base Oracle plus the instructions these functions and 00121A28 use."""

    def __init__(self, elf):
        self.recording = False
        super().__init__(elf)
        self.events = []      # (kind, address, args, snapshot)
        self.trace = []       # (caller, callee, a0..a3 low 32 bits)
        self.oob = None       # (event count, snapshot) at the first D_00810730 overrun
        self.config = {}
        self.garbage = 0x5EED0000

    # ---- memory checks -------------------------------------------------
    def snapshot(self):
        out = bytearray(SNAP)
        for address, offset in OFFSET.items():
            out[offset] = Oracle.load(self, address, 1)
        return bytes(out)

    def allowed_stack(self, address, size):
        return STACK-0x200 <= address and address+size <= STACK

    def load(self, address, size=4):
        if self.recording:
            span = range(address, address+size)
            if address in READERS:
                assert size == READERS[address], ('reader width', hex(address), size)
                self.events.append(('read', address, (), self.snapshot()))
            elif address in OOB and size == 1:
                if self.oob is None:
                    self.oob = (len(self.events), self.snapshot())
            elif all(a in OWNED for a in span) or self.allowed_stack(address, size):
                pass
            elif SLOT <= address and address+size <= SLOT+4:
                pass
            elif 0x100000 <= address and address+size <= 0x275B00:
                pass
            else:
                raise AssertionError(('unmodelled load', hex(address), size))
        return super().load(address, size)

    def save(self, address, value, size=4):
        if self.recording:
            if address in STORES:
                assert size == STORES[address], ('store width', hex(address), size)
                self.events.append(('store', address, (value & ((1 << (8*size))-1),),
                                    self.snapshot()))
            elif all(a in OWNED for a in range(address, address+size)):
                pass
            elif self.allowed_stack(address, size):
                pass
            else:
                raise AssertionError(('unmodelled store', hex(address), size))
        super().save(address, value, size)

    # ---- ISA extensions ------------------------------------------------
    def plain(self, word):
        r = self.r
        op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        sa, fn = word >> 6 & 31, word & 63
        imm = signed(word & 0xFFFF, 16)
        if op == 11:  # sltiu (immediate sign-extended, compared unsigned)
            r[rt] = int((r[rs] & M64) < (imm & M64))
        elif op == 32:  # lb
            r[rt] = signed(self.load((r[rs]+imm) & 0xFFFFFFFF, 1), 8) & 0xFFFFFFFF
        elif op == 37:  # lhu
            r[rt] = self.load((r[rs]+imm) & 0xFFFFFFFF, 2)
        elif op == 63:  # sd
            self.save((r[rs]+imm) & 0xFFFFFFFF, r[rt] & M64, 8)
        elif op == 0 and fn == 43:  # sltu
            r[rd] = int((r[rs] & M64) < (r[rt] & M64))
        elif op == 0 and fn == 56:  # dsll
            r[rd] = (r[rt] << sa) & M64
        elif op == 0 and fn == 60:  # dsll32
            r[rd] = (r[rt] << (sa+32)) & M64
        elif op == 28 and fn == 9 and sa == 0x0E:  # pcpyld rd, rs, rt
            r[rd] = ((r[rs] & M64) << 64) | (r[rt] & M64)
        elif op == 28 and fn == 41 and sa == 0x1B:  # pcpyh rd, rt
            lo, hi = r[rt] & 0xFFFF, r[rt] >> 64 & 0xFFFF
            r[rd] = sum(lo << (16*i) for i in range(4)) | sum(hi << (64+16*i) for i in range(4))
        else:
            super().plain(word)
        r[0] = 0

    # ---- execution -----------------------------------------------------
    @staticmethod
    def function_of(pc):
        for start, size in FUNCS.items():
            if start <= pc < start+size:
                return start
        raise AssertionError(('pc outside the executed functions', hex(pc)))

    def stub(self, caller, callee):
        args = tuple(self.r[4+i] & M64 for i in range(4))
        self.trace.append((caller, callee) + tuple(a & 0xFFFFFFFF for a in args))
        self.events.append(('call', callee, args, self.snapshot()))
        for reg in CALLER_SAVED:
            self.garbage += 1
            self.r[reg] = self.garbage
        if callee in RESULT_CALLEES:
            self.r[2] = self.config['results'][callee] & 0xFFFFFFFF
        if callee == 0x21B550 and self.config.get('clear_bd8'):
            Oracle.save(self, 0x275BD8, 0, 1)
        if self.config.get('perturb'):
            for address in OFFSET:
                Oracle.save(self, address, (Oracle.load(self, address, 1)+1) & 0xFF, 1)

    def execute(self, entry):
        self.r[31] = RETURN
        self.r[29] = STACK
        self.recording = True
        pc = entry
        for _ in range(20000):
            if pc == RETURN:
                self.recording = False
                return self.r[2] & 0xFFFFFFFF
            self.function_of(pc)
            word = Oracle.load(self, pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            imm = signed(word & 0xFFFF, 16)
            if op in (1, 4, 5, 6, 7, 20, 21, 22, 23):
                if op == 1:
                    assert rt in (0, 1), hex(word)
                    taken = (signed(self.r[rs]) < 0) == (rt == 0)
                elif op in (4, 5, 20, 21):
                    taken = (self.r[rs] & M64) == (self.r[rt] & M64)
                    taken = taken if op in (4, 20) else not taken
                elif op in (6, 22):
                    taken = signed(self.r[rs]) <= 0
                else:
                    taken = signed(self.r[rs]) > 0
                likely = op >= 20
                if not likely or taken:
                    self.plain(Oracle.load(self, pc+4))
                pc = pc+4+imm*4 if taken else pc+8
                continue
            if op in (2, 3):
                target = (pc & 0xF0000000) | (word & 0x3FFFFFF) << 2
                caller = self.function_of(pc)
                if op == 3:
                    self.r[31] = pc+8
                self.plain(Oracle.load(self, pc+4))
                if target in FUNCS:
                    if op == 3:
                        self.trace.append((caller, target) +
                                          tuple(self.r[4+i] & 0xFFFFFFFF for i in range(4)))
                    pc = target
                else:
                    assert op == 3, ('tail jump out of the executed set', hex(pc))
                    self.stub(caller, target)
                    pc = pc+8
                continue
            if op == 0 and word & 63 == 8:  # jr
                target = self.r[rs] & 0xFFFFFFFF
                self.plain(Oracle.load(self, pc+4))
                pc = target
                continue
            try:
                self.plain(word)
            except AssertionError as error:
                raise AssertionError(hex(pc), error) from error
            pc += 4
        raise AssertionError('original did not return')


def run_original(elf, entry, snap, config):
    o = TaskOracle(elf)
    for address, offset in OFFSET.items():
        o.save(address, snap[offset], 1)
    o.save(SLOT, TASK)
    o.save(0x28A9A0, config['fade'] & 0xFFFF, 2)
    o.save(0x282157, config['busy'], 1)
    o.config = config
    ret = o.execute(entry)
    return {'ret': ret, 'events': o.events, 'trace': o.trace, 'final': o.snapshot(), 'oob': o.oob}


# ------------------------------------------------------------------ native shim
MAXEV = 64
SHIM = r'''
#include <string.h>
#include "game/em_scene_task.h"

#define SNAP %(snap)d
#define MAXEV %(maxev)d
#define TASK_BASE 0x%(task)Xu

typedef struct { uint32_t kind, address; uint64_t a[4]; uint8_t snap[SNAP]; } Ev;
typedef struct { uint32_t caller, callee, a[4]; } Tr;
typedef struct {
    int32_t entry, fail_at, perturb, clear_bd8, r_1AD1A0, r_1AD230, r_21B550, fade, busy;
    uint32_t null_address;
    int32_t prefault, null_user, no_table;
} Cfg;
typedef struct {
    int32_t ret, nev, ntr, fault_code, overflow;
    uint32_t fault_address;
    uint8_t snap[SNAP];
    Ev ev[MAXEV];
    Tr tr[MAXEV];
} Out;

typedef struct { EmSceneState *s; uint8_t *user; const Cfg *cfg; Out *out; } Ctx;

/* Layout, in order: see LAYOUT in the Python test. */
static void io(EmSceneState *s, uint8_t *user, uint8_t *b, int write)
{
    size_t o = 0;
#define BYTES(p, n) do { if (write) memcpy((p), b + o, (n)); else memcpy(b + o, (p), (n)); o += (n); } while (0)
#define LE(field, n) do { uint32_t v_ = 0; \
        if (write) { for (int i_ = 0; i_ < (n); ++i_) v_ |= (uint32_t)b[o + i_] << (8 * i_); field = v_; } \
        else { v_ = (uint32_t)(field); for (int i_ = 0; i_ < (n); ++i_) b[o + i_] = (uint8_t)(v_ >> (8 * i_)); } \
        o += (n); } while (0)
    BYTES(user, 24);
    BYTES(s->req, 0x48);
    LE(s->d810700, 1); LE(s->d810701, 1); LE(s->d810702, 1);
    BYTES(s->d810730, 0x20);
    LE(s->d810750, 4); LE(s->spad3B68, 4); LE(s->spad3B84, 2); LE(s->spad3B8A, 2);
    LE(s->spad3B8C, 1); LE(s->spad3B8D, 1); LE(s->spad3B8E, 1); LE(s->spad3B8F, 1);
    LE(s->spad3B90, 1); LE(s->spad3B91, 1); LE(s->spad3B92, 1); LE(s->spad3B93, 1);
    LE(s->spad3258, 4); LE(s->spad31F4, 4);
    LE(s->d275BD8, 1); LE(s->d275BDC, 1); LE(s->d275BE0, 1); LE(s->d8101E4, 1);
    LE(s->d810E74, 2); LE(s->d810E70, 2); LE(s->d810E50, 1);
#undef BYTES
#undef LE
    (void)o;
}

static int hit(void *ctx, uint32_t kind, uint32_t address, uint64_t a0, uint64_t a1, uint64_t a2,
               uint64_t a3, int result)
{
    Ctx *c = ctx;
    if (c->out->nev >= MAXEV) { c->out->overflow = 1; return -1; }
    int index = c->out->nev++;
    Ev *e = &c->out->ev[index];
    e->kind = kind; e->address = address;
    e->a[0] = a0; e->a[1] = a1; e->a[2] = a2; e->a[3] = a3;
    io(c->s, c->user, e->snap, 0);
    if (index == c->cfg->fail_at)
        return -1;
    if (kind == 1) {
        if (address == 0x21B550u && c->cfg->clear_bd8)
            c->s->d275BD8 = 0;
        if (c->cfg->perturb) {
            uint8_t b[SNAP];
            io(c->s, c->user, b, 0);
            for (int i = 0; i < SNAP; ++i) b[i] = (uint8_t)(b[i] + 1);
            io(c->s, c->user, b, 1);
        }
    }
    return result;
}

#define U(x) ((uint64_t)(uint32_t)(int32_t)(x))
#define CALL0(addr) static int w_##addr(void *c) { return hit(c, 1, 0x##addr##u, 0, 0, 0, 0, 0); }
CALL0(001AD4D0) CALL0(001AD740) CALL0(001D1EF0) CALL0(001FABB0) CALL0(001FBC50)
CALL0(001FC9B0) CALL0(0021B180) CALL0(0021B840) CALL0(001D2880) CALL0(001FAB50)
static int w_001AD1A0(void *c) { return hit(c, 1, 0x1AD1A0u, 0, 0, 0, 0, ((Ctx *)c)->cfg->r_1AD1A0); }
static int w_001AD230(void *c) { return hit(c, 1, 0x1AD230u, 0, 0, 0, 0, ((Ctx *)c)->cfg->r_1AD230); }
static int w_0021B550(void *c) { return hit(c, 1, 0x21B550u, 0, 0, 0, 0, ((Ctx *)c)->cfg->r_21B550); }
static int w_001AEDB0(void *c, uint8_t a) { return hit(c, 1, 0x1AEDB0u, U(a), 0, 0, 0, 0); }
static int w_001AED80(void *c, uint8_t a) { return hit(c, 1, 0x1AED80u, U(a), 0, 0, 0, 0); }
static int w_001FF080(void *c, int a, int b) { return hit(c, 1, 0x1FF080u, U(a), U(b), 0, 0, 0); }
static int w_001D2830(void *c, int a, int b) { return hit(c, 1, 0x1D2830u, U(a), U(b), 0, 0, 0); }
static int w_001FA790(void *c, int a, int b) { return hit(c, 1, 0x1FA790u, U(a), U(b), 0, 0, 0); }
static int w_001AEE10(void *c, int16_t a, uint8_t b) { return hit(c, 1, 0x1AEE10u, U(a), U(b), 0, 0, 0); }
static int w_001AEDE0(void *c, int16_t a, uint8_t b) { return hit(c, 1, 0x1AEDE0u, U(a), U(b), 0, 0, 0); }
static int w_001AEBA0(void *c, int16_t a) { return hit(c, 1, 0x1AEBA0u, U(a), 0, 0, 0, 0); }
static int w_001AB790(void *c, uint32_t fn) { return hit(c, 1, 0x1AB790u, fn, 0, 0, 0, 0); }
static int w_001ABF90(void *c, uint64_t a, uint64_t b, uint64_t d, uint64_t e)
{ return hit(c, 1, 0x1ABF90u, a, b, d, e, 0); }
static int16_t r_0028A9A0(void *c) { hit(c, 3, 0x28A9A0u, 0, 0, 0, 0, 0); return (int16_t)((Ctx *)c)->cfg->fade; }
static uint8_t r_00282157(void *c) { hit(c, 3, 0x282157u, 0, 0, 0, 0, 0); return (uint8_t)((Ctx *)c)->cfg->busy; }
static int s_00821058(void *c, uint8_t v) { return hit(c, 2, 0x821058u, v, 0, 0, 0, 0); }
static int s_00275C78(void *c, uint8_t v) { return hit(c, 2, 0x275C78u, v, 0, 0, 0, 0); }
static int s_00810D38(void *c, int32_t v) { return hit(c, 2, 0x810D38u, (uint32_t)v, 0, 0, 0, 0); }

static void trace(void *ctx, uint32_t caller, uint32_t callee, uint32_t a0, uint32_t a1,
                  uint32_t a2, uint32_t a3)
{
    Ctx *c = ctx;
    if (c->out->ntr >= MAXEV) { c->out->overflow = 1; return; }
    Tr *t = &c->out->tr[c->out->ntr++];
    t->caller = caller; t->callee = callee;
    t->a[0] = a0; t->a[1] = a1; t->a[2] = a2; t->a[3] = a3;
}

int shim_sizes(int which) { return which == 0 ? (int)sizeof(Out) : which == 1 ? (int)sizeof(Ev) : (int)sizeof(Cfg); }

int scene_task_shim(const Cfg *cfg, const uint8_t *snap_in, Out *out)
{
    static EmSceneState s;
    static uint8_t user[24];
    memset(&s, 0xA5, sizeof s);  /* padding/unlisted bytes must not matter */
    memset(out, 0, sizeof *out);
    io(&s, user, (uint8_t *)snap_in, 1);
    s.fault.address = 0; s.fault.code = EM_SCENE_FAULT_NONE;
    if (cfg->prefault) em_scene_fault(&s, 0xDEADu, EM_SCENE_FAULT_WORKER_FAILED);
    Ctx c = { &s, user, cfg, out };
    EmSceneWorkers w;
    memset(&w, 0, sizeof w);
    w.ctx = &c; w.trace = trace;
    w.r_0028A9A0 = r_0028A9A0; w.r_00282157 = r_00282157;
    w.s_00821058 = s_00821058; w.s_00275C78 = s_00275C78; w.s_00810D38 = s_00810D38;
    w.w_001AD1A0 = w_001AD1A0; w.w_001AD230 = w_001AD230; w.w_001AD4D0 = w_001AD4D0;
    w.w_001AD740 = w_001AD740; w.w_001D1EF0 = w_001D1EF0; w.w_001FABB0 = w_001FABB0;
    w.w_001FBC50 = w_001FBC50; w.w_001FC9B0 = w_001FC9B0; w.w_0021B180 = w_0021B180;
    w.w_0021B840 = w_0021B840; w.w_001D2880 = w_001D2880; w.w_001FAB50 = w_001FAB50;
    w.w_0021B550 = w_0021B550; w.w_001AEDB0 = w_001AEDB0; w.w_001AED80 = w_001AED80;
    w.w_001FF080 = w_001FF080; w.w_001D2830 = w_001D2830; w.w_001FA790 = w_001FA790;
    w.w_001AEE10 = w_001AEE10; w.w_001AEDE0 = w_001AEDE0; w.w_001AEBA0 = w_001AEBA0;
    w.w_001AB790 = w_001AB790; w.w_001ABF90 = w_001ABF90;
    switch (cfg->null_address) {
#define NUL(addr, field) case 0x##addr##u: w.field = NULL; break;
    NUL(28A9A0, r_0028A9A0) NUL(282157, r_00282157) NUL(821058, s_00821058)
    NUL(275C78, s_00275C78) NUL(810D38, s_00810D38) NUL(1AD1A0, w_001AD1A0)
    NUL(1AD230, w_001AD230) NUL(1AD4D0, w_001AD4D0) NUL(1AD740, w_001AD740)
    NUL(1D1EF0, w_001D1EF0) NUL(1FABB0, w_001FABB0) NUL(1FBC50, w_001FBC50)
    NUL(1FC9B0, w_001FC9B0) NUL(21B180, w_0021B180) NUL(21B840, w_0021B840)
    NUL(1D2880, w_001D2880) NUL(1FAB50, w_001FAB50) NUL(21B550, w_0021B550)
    NUL(1AEDB0, w_001AEDB0) NUL(1AED80, w_001AED80) NUL(1FF080, w_001FF080)
    NUL(1D2830, w_001D2830) NUL(1FA790, w_001FA790) NUL(1AEE10, w_001AEE10)
    NUL(1AEDE0, w_001AEDE0) NUL(1AEBA0, w_001AEBA0) NUL(1AB790, w_001AB790)
    NUL(1ABF90, w_001ABF90)
#undef NUL
    default: break;
    }
    const EmSceneWorkers *wp = cfg->no_table ? NULL : &w;
    uint8_t *u = cfg->null_user ? NULL : user;
    int r;
    switch (cfg->entry) {
    case 0x1ACEC0: r = em_sf_001ACEC0(&s, u, wp); break;
    case 0x1AD250: r = em_sf_001AD250(&s, u, wp); break;
    case 0x1AD360: r = em_sf_001AD360(&s, u, wp); break;
    case 0x1ADF50: r = em_sf_001ADF50(&s, u, wp); break;
    case 0x1AD4E0: r = em_sf_001AD4E0(&s, u, wp); break;
    case 0x1AD010: r = em_sf_001AD010(&s, u, wp); break;
    case 0x1AD140: r = em_sf_001AD140(&s, u, wp); break;
    case 0x1ADF00: r = em_sf_001ADF00(&s, wp); break;
    case 0x1AFCF0: r = em_sf_001AFCF0(&s, wp); break;
    default: r = -100; break;
    }
    out->ret = r;
    out->fault_address = s.fault.address;
    out->fault_code = s.fault.code;
    io(&s, user, out->snap, 0);
    return r;
}
'''


class Ev(C.Structure):
    _fields_ = [('kind', C.c_uint32), ('address', C.c_uint32), ('a', C.c_uint64*4),
                ('snap', C.c_uint8*SNAP)]


class Tr(C.Structure):
    _fields_ = [('caller', C.c_uint32), ('callee', C.c_uint32), ('a', C.c_uint32*4)]


class Cfg(C.Structure):
    _fields_ = [(n, C.c_int32) for n in ('entry', 'fail_at', 'perturb', 'clear_bd8', 'r_1AD1A0',
                                         'r_1AD230', 'r_21B550', 'fade', 'busy')] + \
               [('null_address', C.c_uint32)] + \
               [(n, C.c_int32) for n in ('prefault', 'null_user', 'no_table')]


class Out(C.Structure):
    _fields_ = [(n, C.c_int32) for n in ('ret', 'nev', 'ntr', 'fault_code', 'overflow')] + \
               [('fault_address', C.c_uint32), ('snap', C.c_uint8*SNAP),
                ('ev', Ev*MAXEV), ('tr', Tr*MAXEV)]


class Native:
    def __init__(self):
        out = ROOT/'build/scene_task_reference'
        out.mkdir(parents=True, exist_ok=True)
        ext = 'dylib' if sys.platform == 'darwin' else 'so'
        link = '-dynamiclib' if sys.platform == 'darwin' else '-shared'
        (out/'shim.c').write_text(SHIM % {'snap': SNAP, 'maxev': MAXEV, 'task': TASK})
        lib = out/f'scene_task.{ext}'
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', link,
                        '-Isrc', 'src/game/em_scene_task.c', str(out/'shim.c'), '-o', str(lib)],
                       cwd=ROOT, check=True)
        self.lib = C.CDLL(str(lib))
        self.lib.scene_task_shim.argtypes = [C.POINTER(Cfg), C.c_char_p, C.POINTER(Out)]
        assert self.lib.shim_sizes(0) == C.sizeof(Out)
        assert self.lib.shim_sizes(1) == C.sizeof(Ev)
        assert self.lib.shim_sizes(2) == C.sizeof(Cfg)
        self.out = Out()

    def run(self, entry, snap, config, fail_at=-1, null_address=0, prefault=0, null_user=0,
            no_table=0):
        cfg = Cfg(entry, fail_at, int(config.get('perturb', 0)), int(config.get('clear_bd8', 0)),
                  config['results'][0x1AD1A0], config['results'][0x1AD230],
                  config['results'][0x21B550], config['fade'], config['busy'], null_address,
                  prefault, null_user, no_table)
        out = self.out
        self.lib.scene_task_shim(C.byref(cfg), bytes(snap), C.byref(out))
        assert not out.overflow
        events = []
        for e in out.ev[:out.nev]:
            kind = {1: 'call', 2: 'store', 3: 'read'}[e.kind]
            events.append((kind, e.address, tuple(e.a), bytes(e.snap)))
        trace = [(t.caller, t.callee) + tuple(t.a) for t in out.tr[:out.ntr]]
        return {'ret': out.ret, 'events': events, 'trace': trace, 'final': bytes(out.snap),
                'fault': (out.fault_address, out.fault_code)}


# ------------------------------------------------------------------ comparison
def event_key(event):
    kind, address, args, snap = event
    if kind == 'call':
        n = ARGS.get(address, 0)
        args = tuple(a & M64 for a in args[:n])
    elif kind == 'store':
        args = (args[0],)
    else:
        args = ()
    return kind, address, args, snap


def trace_key(entry):
    caller, callee = entry[0], entry[1]
    n = TRACE_ARGS.get((caller, callee), ARGS.get(callee, 0))
    return (caller, callee) + tuple(entry[2:2+n])


def describe(event):
    kind, address, args, _ = event
    return f'{kind} {address:06X} {[hex(a) for a in args]}'


def compare_events(label, got, want):
    got_k, want_k = [event_key(e) for e in got], [event_key(e) for e in want]
    if got_k != want_k:
        for i, (g, w) in enumerate(zip(got_k, want_k)):
            if g != w:
                diff = [hex(j) for j in range(SNAP) if g[3][j] != w[3][j]]
                raise AssertionError((label, 'event', i, describe(got[i]), describe(want[i]),
                                      'snapshot bytes differing', diff[:8]))
        raise AssertionError((label, 'event count', [describe(e) for e in got],
                              [describe(e) for e in want]))


def compare_run(label, entry, native, original):
    compare_events(label, native['events'], original['events'])
    got_t = [trace_key(t) for t in native['trace']]
    want_t = [trace_key(t) for t in original['trace']]
    assert got_t == want_t, (label, 'trace', [tuple(map(hex, t)) for t in got_t],
                             [tuple(map(hex, t)) for t in want_t])
    if native['final'] != original['final']:
        diff = [(hex(j), native['final'][j], original['final'][j]) for j in range(SNAP)
                if native['final'][j] != original['final'][j]]
        raise AssertionError((label, 'final state', diff[:12]))
    want_ret = original['ret'] if entry in RETURNS_VALUE else 0
    assert native['ret'] == want_ret, (label, 'return', native['ret'], original['ret'])
    assert native['fault'] == (0, 0), (label, 'unexpected fault', native['fault'])


# ------------------------------------------------------------------ oracle validation
def validate_extensions(elf):
    checks = 0
    # lb sign-extends, lhu zero-extends; sltiu/sltu compare unsigned.
    for value in (0, 1, 0x7F, 0x80, 0xFF, 0x1234, 0x8000, 0xFFFF):
        for op, size, expected in ((32, 1, signed(value & 0xFF, 8) & 0xFFFFFFFF),
                                   (37, 2, value & 0xFFFF)):
            o = TaskOracle(elf)
            o.save(0x500100, value & ((1 << (8*size))-1), size)
            o.save(0x500102, 0xA5A5, 2)
            o.r[4] = 0x500100
            o.plain((op << 26) | (4 << 21) | (2 << 16))
            assert o.r[2] == expected, (op, hex(value), hex(o.r[2]))
            checks += 1
    for a, b in ((0, 1), (1, 0), (0xFFFFFFFF, 1), (1, 0xFFFFFFFF), (5, 5), (0x80000000, 0x7FFFFFFF)):
        o = TaskOracle(elf)
        o.r[4], o.r[5] = a, b
        o.plain((4 << 21) | (5 << 16) | (2 << 11) | 43)  # sltu v0, a0, a1
        assert o.r[2] == int(a < b), (hex(a), hex(b))
        for imm in (0, 6, 0x7FFF, 0xFFFF):
            o.plain((11 << 26) | (4 << 21) | (3 << 16) | imm)  # sltiu v1, a0, imm
            assert o.r[3] == int(a < (signed(imm, 16) & M64)), (hex(a), hex(imm))
            checks += 1
    # The original memset 00121A28 (sltiu, dsll, pcpyh, pcpyld, sq, sd, sb, beql).
    rng = random.Random(0x121A28)
    for align in range(16):
        for length in (0, 1, 7, 8, 9, 15, 16, 17, 31, 32, 33, 0x48, 80):
            for value in (0, 0xFF, rng.randrange(256), 0x80 | rng.randrange(128)):
                o = TaskOracle(elf)
                base = 0x500000+align
                for i in range(-16, length+16):
                    o.save(base+i, 0xC3, 1)
                o.r[4], o.r[5], o.r[6] = base, value | 0x1200, length  # a1 upper bits ignored
                o.r[29] = STACK
                o.r[31] = RETURN
                pc = 0x121A28
                for _ in range(2000):
                    if pc == RETURN:
                        break
                    word = Oracle.load(o, pc)
                    op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
                    imm = signed(word & 0xFFFF, 16)
                    if op in (4, 5, 20, 21):
                        taken = ((o.r[rs] & M64) == (o.r[rt] & M64)) == (op in (4, 20))
                        if op < 20 or taken:
                            o.plain(Oracle.load(o, pc+4))
                        pc = pc+4+imm*4 if taken else pc+8
                    elif op == 0 and word & 63 == 8:
                        target = o.r[rs]
                        o.plain(Oracle.load(o, pc+4))
                        pc = target
                    else:
                        o.plain(word)
                        pc += 4
                else:
                    raise AssertionError('memset did not return')
                assert o.r[2] & 0xFFFFFFFF == base, ('memset return', hex(o.r[2]))
                for i in range(-16, length+16):
                    want = value if 0 <= i < length else 0xC3
                    got = Oracle.load(o, base+i, 1)
                    assert got == want, ('memset', align, length, hex(value), i, got, want)
                checks += 1
    return checks


# ------------------------------------------------------------------ cases
def background(rng, noise):
    snap = bytearray(rng.randrange(256) for _ in range(SNAP)) if noise else bytearray(SNAP)
    # D_00810730: an area table whose bytes carry bit 7, so & 0x7F is visible.
    for i in range(0x20):
        put(snap, 0x810730+i, 0x80 | rng.randrange(128) if noise or i % 3 else rng.randrange(256))
    return snap


def random_config(rng):
    return {'results': {0x1AD1A0: rng.choice((0, 4, 1)), 0x1AD230: rng.choice((0, 4)),
                        0x21B550: rng.choice((0, 1, 4))},
            'fade': rng.choice((0, 1, 2, 3, -1, -32768, 0x7FFF)),
            'busy': rng.choice((0, 1, 0x80, 0xFF)),
            'clear_bd8': rng.randrange(2), 'perturb': 0}


def cases():
    """Yield (entry, snap, config, label)."""
    rng = random.Random(0x1ACEC0)
    arm = (0, 1, 2, 3, 4, 5, 6, 0xFF)
    # 001ACEC0 over +8 x +9 x +A (the whole chain), noise and zero backgrounds.
    for t8, t9, tA in itertools.product((0, 1, 2, 3, 4, 0xFF), arm, arm):
        if t8 != 3 and (t9, tA) != (0, 0) and rng.randrange(4):
            continue  # +9/+A only matter below +8 == 3; keep a sample
        for sample in range(6):
            snap = background(rng, sample % 2)
            put(snap, TASK+8, t8); put(snap, TASK+9, t9); put(snap, TASK+0xA, tA)
            config = random_config(rng)
            put(snap, 0x275BE0, rng.choice((0, 1, 0x80)))
            put(snap, 0x275BD8, rng.choice((0, 1)))
            yield 0x1ACEC0, snap, config, f'1ACEC0 +8={t8:X} +9={t9:X} +A={tA:X} s{sample}'
    # 001AD250 directly.
    for t9, tA in itertools.product(arm, arm):
        for sample in range(4):
            snap = background(rng, sample % 2)
            put(snap, TASK+9, t9); put(snap, TASK+0xA, tA)
            put(snap, 0x275BD8, sample >> 1)
            yield 0x1AD250, snap, random_config(rng), f'1AD250 +9={t9:X} +A={tA:X} s{sample}'
    # 001ADF50: +A x BD8 x 0021B550 x clears-BD8.
    for tA, bd8, res, clear in itertools.product((0, 1, 2, 3, 0xFF), (0, 1, 2), (0, 1, 4), (0, 1)):
        for sample in range(2):
            snap = background(rng, sample)
            put(snap, TASK+0xA, tA); put(snap, 0x275BD8, bd8)
            config = random_config(rng)
            config['results'][0x21B550] = res
            config['clear_bd8'] = clear
            yield 0x1ADF50, snap, config, f'1ADF50 +A={tA:X} BD8={bd8} r={res} clr={clear} s{sample}'
    # 001AD360: +A x D_00282157.
    for tA, busy in itertools.product(arm, (0, 1, 0x80, 0xFF)):
        for sample in range(3):
            snap = background(rng, sample % 2)
            put(snap, TASK+0xA, tA)
            config = random_config(rng)
            config['busy'] = busy
            yield 0x1AD360, snap, config, f'1AD360 +A={tA:X} busy={busy:X} s{sample}'
    # 001AD4E0: +A x BD8 x fade x +0x18 x E74.
    for tA, bd8, fade, count, e74 in itertools.product(
            (0, 1, 2, 3, 4, 5, 0xFF), (0, 1), (0, 1, 2, 3, -1), (0, 1, 2, 0xF0, 0xFFFF),
            (0, 0x40, 0xFFBF, 0xFFFF)):
        snap = background(rng, rng.randrange(2))
        put(snap, TASK+0xA, tA); put(snap, 0x275BD8, bd8)
        put(snap, TASK+0x18, count, 2); put(snap, 0x810E74, e74, 2)
        config = random_config(rng)
        config['fade'] = fade
        yield 0x1AD4E0, snap, config, f'1AD4E0 +A={tA:X} BD8={bd8} fade={fade} n={count:X} e74={e74:X}'
    # 001AD010: B8 x 3B93 x B6 x B5 x B7.
    for b8, st, b6, b5, b7 in itertools.product((0, 1, 2, 3, 0xFF), (0, 1, 2, 3, 0xFF),
                                                (0, 3, 0x7F, 0x80, 0xFE, 0xFF),
                                                (0, 0x0B, 0x1F, 0x20, 0x80, 0xFF), (0, 5, 0xFF)):
        snap = background(rng, rng.randrange(2))
        put(snap, 0x8106B8, b8); put(snap, 0x70003B93, st); put(snap, 0x8106B6, b6)
        put(snap, 0x8106B5, b5); put(snap, 0x8106B7, b7)
        yield 0x1AD010, snap, random_config(rng), \
            f'1AD010 B8={b8:X} 3B93={st:X} B6={b6:X} B5={b5:X} B7={b7:X}'
    # 001AD140, 001ADF00, 001AFCF0 over backgrounds.
    for entry in (0x1AD140, 0x1ADF00, 0x1AFCF0):
        for sample in range(24):
            snap = background(rng, sample % 3 != 0)
            yield entry, snap, random_config(rng), f'{entry:06X} s{sample}'


def main():
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    extension_checks = validate_extensions(elf)
    native = Native()

    counts = {f'{e:06X}': 0 for e in ENTRIES}
    runs = fault_runs = oob_cases = 0
    arms_seen = set()
    for entry, snap, config, label in cases():
        for perturb in (0, 1):
            config = dict(config, perturb=perturb)
            original = run_original(elf, entry, snap, config)
            got = native.run(entry, snap, config)
            if original['oob'] is not None:
                # 001AD010 read D_00810730[B5 >= 0x20]: the native faults there.
                k, oob_snap = original['oob']
                assert entry == 0x1AD010 and get(snap, 0x8106B5) >= 0x20, label
                assert got['ret'] == -1 and got['fault'] == (0x1AD010, FAULT_INDEX), (label, got['fault'])
                compare_events(label+' oob', got['events'], original['events'][:k])
                assert got['final'] == oob_snap, (label, 'oob state')
                oob_cases += 1
                runs += 1
                continue
            compare_run(label+f' p{perturb}', entry, got, original)
            runs += 1
            counts[f'{entry:06X}'] += 1
            arms_seen.add((entry, tuple(t[1] for t in original['trace'])))
            if perturb:
                continue
            # Fail-stop against the original's own state at each event.
            events = original['events']
            for k, (kind, address, _, snapshot) in enumerate(events):
                if kind != 'read':
                    f = native.run(entry, snap, config, fail_at=k)
                    assert f['ret'] == -1 and f['fault'] == (address, FAULT_FAILED), \
                        (label, 'fail', k, f['fault'])
                    compare_events(label+f' fail@{k}', f['events'], events[:k+1])
                    assert f['final'] == snapshot, (label, 'fail state', k)
                    fault_runs += 1
            first = {}
            for k, (_, address, _, _) in enumerate(events):
                first.setdefault(address, k)
            for address, k in first.items():
                f = native.run(entry, snap, config, null_address=address)
                assert f['ret'] == -1 and f['fault'] == (address, FAULT_NULL), \
                    (label, 'null', hex(address), f['fault'])
                compare_events(label+f' null {address:X}', f['events'], events[:k])
                assert f['final'] == events[k][3], (label, 'null state', hex(address))
                fault_runs += 1
            f = native.run(entry, snap, config, no_table=1)
            if events:
                assert f['ret'] == -1 and f['fault'] == (events[0][1], FAULT_NULL), (label, 'no table')
                assert f['final'] == events[0][3], (label, 'no table state')
            else:
                assert f['ret'] == (original['ret'] if entry in RETURNS_VALUE else 0), label
                assert f['final'] == original['final'], (label, 'no table state')
            f = native.run(entry, snap, config, prefault=1)
            assert f['ret'] == -1 and f['fault'] == (0xDEAD, FAULT_FAILED) and not f['events'] \
                and not f['trace'] and f['final'] == bytes(snap), (label, 'prefault')
            if entry not in (0x1ADF00, 0x1AFCF0):
                f = native.run(entry, snap, config, null_user=1)
                assert f['ret'] == -1 and f['fault'] == (entry, FAULT_INDEX) and not f['events'] \
                    and f['final'] == bytes(snap), (label, 'null user')
            fault_runs += 3

    report = {
        'status': 'PASS',
        'executed_originals': [f'0x{e:06X}' for e in ENTRIES] + ['0x121A28'],
        'compared_runs': runs,
        'runs_per_entry_excluding_oob': counts,
        'distinct_call_sequences': len(arms_seen),
        'fail_stop_runs': fault_runs,
        'd810730_overrun_fault_cases': oob_cases,
        'oracle_extension_checks': extension_checks,
        'compared': ['ordered events (callee, declared args, owned state at the call)',
                     'trace (caller, callee, call-site args)', 'final owned state',
                     'return value', 'fault latch'],
        'original_elf_sha256': ELF_SHA256,
    }
    out = ROOT/'build/scene_task_reference'
    (out/'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
