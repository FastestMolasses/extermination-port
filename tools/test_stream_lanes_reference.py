#!/usr/bin/env python3
"""Original EE stream lanes against the native em_stream_lanes_original.

The ORIGINAL instructions of 001F9CF0, 001FA0D0, 001FA330, 001FA5F0,
001FA570, 001FA790, 001FABF0, 001FAD70, 001FAAC0, 001FAB50, 001FAB80,
001FABB0, 001FD470, 001FAE70, 001FB0B0, 00119828 and the leaves they reach
(0011A608, 0011A6A0, 0011A6E8, 0011A730, 001281C0, 00128250, 001278C0) run in
a bounded EE interpreter defined here, over captured original RAM (the
user's ../Extermination/build/startup-reference/ and build/s87/route/ images,
never copied into the repository). Code bytes come from the user's pinned
boot ELF; the capture's copy of every executed function is checked to be
identical. COP1 arithmetic is tools/ee_float_model.py (docs/EE_FLOAT_MODEL.md).

Callees outside the module are recorded as worker calls with scripted
results, identically on both sides: 001157F0 (IOP command queue), 00122BB8
(LCG), 001FC280, 001FBC50, 00113280 / 00112610 / 00112D18 / 00113478 (disc
stream) and 00121A28 (memset, performed and argument-checked).

Every call compares: the 0x1C0 bytes 0x281FD0..0x282190 (four lane records
and the lane block; the native side writes only its modelled fields into a
copy of the input, so an original write to any unmodelled byte fails), the
ring D_00281CF0[16], D_00275B2C/30/34, the globals the functions touch, the
ordered worker calls with every argument, and the return code. Every load
and store the original makes outside the stack is checked against the
modelled read/write sets.

Capture evidence (no oracle): the lane-0 fade steps and volumes captured in
the route images are reproduced by the native 001FABF0 / 001FA330 arithmetic,
and every captured lane's duration / end / count fields by native 001FA790
over the ELF clip row its base sector selects.

No original instruction bytes, disassembly or data are written by this file;
the report in build/ holds only counts.
"""
import ctypes as C
import hashlib
import itertools
import json
import os
import shutil
import tempfile
import zlib
from pathlib import Path
import random
import struct
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ee_float_model as ee  # noqa: E402
from reference_mode import banner, part, pick, select  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
M64 = (1 << 64) - 1
STACK, RETURN = 0x01F00000, 0x0BADF00C
REGION, REGION_SIZE = 0x281FD0, 0x1C0
RING, GP0 = 0x281CF0, 0x275B2C
STATUS = 0x281880
MUSIC, VOICE, MUSIC_ROWS, VOICE_ROWS = 0x25DD30, 0x25E170, 68, 179
GLOBALS = ((0x810E90, 4), (0x8106C8, 4), (0x810D38, 4), (0x810700, 1),
           (0x8104E4, 1), (0x8106F4, 1), (0x8106F5, 1))
# Executed original functions (start, size); the capture must hold the ELF's bytes.
EXECUTED = ((0x1F9CF0, 0x3DC), (0x1FA0D0, 0x260), (0x1FA330, 0x234), (0x1FA570, 0x30),
            (0x1FA5F0, 0xA4), (0x1FA790, 0x328), (0x1FAAC0, 0x8C), (0x1FAB50, 0x24),
            (0x1FAB80, 0x2C), (0x1FABB0, 0x34), (0x1FABF0, 0x180), (0x1FAD70, 0xF4),
            (0x1FAE70, 0x158), (0x1FB0B0, 0x10), (0x1FD470, 0x44), (0x119828, 0x18),
            (0x11A608, 0x4C), (0x11A6A0, 0x48), (0x11A6E8, 0x48), (0x11A730, 0x28),
            (0x1278C0, 0x90), (0x1281C0, 0x90), (0x128250, 0x98))
(IOP, RNG, FC280, FBC50, CD13280, CD12610, CD12D18, CD13478) = range(1, 9)

# Lane record fields: offset -> size (the bytes EmStreamLane models).
LANE_FIELDS = {0x00: 1, 0x01: 1, 0x02: 1, 0x03: 1, 0x04: 4, 0x08: 8, 0x14: 4, 0x18: 4,
               0x20: 4, 0x24: 4, 0x28: 4, 0x2C: 4, 0x30: 4, 0x34: 4, 0x38: 4, 0x48: 4,
               0x4C: 4, 0x50: 4, 0x54: 4, 0x58: 4, 0x5C: 1}
BLOCK_FIELDS = {0x124: 4, 0x184: 1, 0x185: 1, 0x186: 1, 0x187: 1, 0x188: 1, 0x18B: 1,
                0x1A8: 4, 0x1AC: 4, 0x1B0: 4, 0x1B8: 4, 0x1BC: 4}


def sx(value, bits=32):
    value &= (1 << bits) - 1
    return value - (1 << bits) if value >> (bits - 1) else value


def u64(value): return value & M64


def s32(value): return u64(sx(value))


def modelled():
    out = set()
    for lane in range(3):
        for off, size in LANE_FIELDS.items():
            out.update(REGION + lane * 0x60 + off + i for i in range(size))
    for off, size in BLOCK_FIELDS.items():
        out.update(REGION + off + i for i in range(size))
    out.update(range(RING, RING + 0x40))
    out.update(range(GP0, GP0 + 4)); out.update((0x275B30, 0x275B34))
    for address, size in GLOBALS:
        out.update(range(address, address + size))
    return out


MODELLED = modelled()
READABLE = MODELLED | set(range(STATUS, STATUS + 0xC0)) | {
    base + 16 * row + off for base, rows in ((MUSIC, MUSIC_ROWS), (VOICE, VOICE_ROWS))
    for row in range(rows) for off in (0, 1, 2, 3, 8, 9, 10, 11, 12, 13, 14, 15)}
WRITABLE = MODELLED


COVERED = set()  # every original instruction address executed (coverage check)
# Words no path reaches: compiler duplicates placed after an unconditional
# branch's delay slot, and the zero padding word ending 001281C0.
UNREACHABLE = {0x1F9E50, 0x1F9F4C, 0x1F9FCC, 0x1FA160, 0x1FA388, 0x1FAD40, 0x1FAE44, 0x12824C}


class EE:
    """Bounded EE interpreter over a captured RAM image with a write overlay."""

    def __init__(self, elf, capture):
        self.elf, self.capture = elf, capture
        self.mem = {}
        self.r = [0] * 32
        self.f = [0] * 32
        self.cond = 0
        self.calls = {}
        self.reads, self.writes = set(), set()

    # -- memory
    def byte(self, a):
        if a in self.mem: return self.mem[a]
        if a < len(self.capture): return self.capture[a]
        raise AssertionError(('unmapped load', hex(a)))

    def load(self, a, size=4, track=True):
        if track and not STACK - 0x1000 <= a < STACK + 0x100:
            self.reads.update(range(a, a + size))
        return sum(self.byte(a + i) << (8 * i) for i in range(size))

    def save(self, a, value, size=4, track=True):
        if track and not STACK - 0x1000 <= a < STACK + 0x100:
            self.writes.update(range(a, a + size))
        for i in range(size):
            self.mem[a + i] = value >> (8 * i) & 255

    def read(self, a, n): return bytes(self.byte(a + i) for i in range(n))

    def write(self, a, data):
        for i, value in enumerate(data): self.mem[a + i] = value

    def code(self, pc):
        offset = pc - 0x100000 + 0x300
        return int.from_bytes(self.elf[offset:offset + 4], 'little')

    # -- execution
    def run(self, entry, args=(), f12=None):
        self.r = [0] * 32
        self.r[28], self.r[29], self.r[31] = 0x27D370, STACK, RETURN
        for i, value in enumerate(args): self.r[4 + i] = s32(value)
        if f12 is not None: self.f[12] = f12 & 0xFFFFFFFF
        pc, steps = entry, 0
        while pc != RETURN:
            steps += 1
            assert steps < 200000, ('original did not return', hex(entry))
            word = self.code(pc)
            COVERED.add(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            imm = sx(word & 0xFFFF, 16)
            if op in (2, 3) or (op == 0 and word & 63 in (8, 9)):
                if op in (2, 3):
                    dest = (pc & 0xF0000000) | (word & 0x3FFFFFF) << 2
                    if op == 3: self.r[31] = pc + 8
                else:
                    dest = self.r[rs] & 0xFFFFFFFF
                    if word & 63 == 9: self.r[word >> 11 & 31] = pc + 8
                self.step(self.code(pc + 4)); COVERED.add(pc + 4)
                if dest in self.calls:
                    self.calls[dest](self)
                    pc = pc + 8 if (op == 3 or (op == 0 and word & 63 == 9)) else self.r[31] & 0xFFFFFFFF
                else:
                    pc = dest
                continue
            likely = False
            if op in (4, 5, 20, 21):
                taken = (self.r[rs] == self.r[rt]) == (op in (4, 20)); likely = op >= 20
            elif op in (6, 7, 22, 23):
                v = sx(self.r[rs], 64); taken = v <= 0 if op in (6, 22) else v > 0; likely = op >= 22
            elif op == 1:
                assert rt in (0, 1, 2, 3), hex(word)
                v = sx(self.r[rs], 64); taken = v < 0 if rt in (0, 2) else v >= 0; likely = rt >= 2
            elif op == 17 and rs == 8:
                taken = self.cond == (rt & 1); likely = bool(rt & 2)
            else:
                self.step(word); pc += 4; continue
            if taken:
                self.step(self.code(pc + 4)); COVERED.add(pc + 4); pc = pc + 4 + imm * 4
            elif likely:
                pc += 8
            else:
                self.step(self.code(pc + 4)); COVERED.add(pc + 4); pc += 8

    def step(self, w):
        r, f = self.r, self.f
        op, rs, rt, rd, sa, fn = w >> 26, w >> 21 & 31, w >> 16 & 31, w >> 11 & 31, w >> 6 & 31, w & 63
        imm = sx(w & 0xFFFF, 16)
        ea = (r[rs] + imm) & 0xFFFFFFFF
        if op == 0:
            a, b = r[rs], r[rt]
            if fn == 0: r[rd] = s32(b << sa)
            elif fn == 2: r[rd] = s32((b & 0xFFFFFFFF) >> sa)
            elif fn == 3: r[rd] = s32(sx(b) >> sa)
            elif fn == 4: r[rd] = s32(b << (a & 31))
            elif fn == 6: r[rd] = s32((b & 0xFFFFFFFF) >> (a & 31))
            elif fn == 7: r[rd] = s32(sx(b) >> (a & 31))
            elif fn == 10:
                if b == 0: r[rd] = a
            elif fn == 11:
                if b != 0: r[rd] = a
            elif fn == 20: r[rd] = u64(b << (a & 63))
            elif fn == 22: r[rd] = u64(b) >> (a & 63)
            elif fn == 23: r[rd] = u64(sx(b, 64) >> (a & 63))
            elif fn == 33: r[rd] = s32(a + b)
            elif fn == 35: r[rd] = s32(a - b)
            elif fn == 36: r[rd] = a & b
            elif fn == 37: r[rd] = a | b
            elif fn == 38: r[rd] = a ^ b
            elif fn == 39: r[rd] = u64(~(a | b))
            elif fn == 42: r[rd] = int(sx(a, 64) < sx(b, 64))
            elif fn == 43: r[rd] = int(u64(a) < u64(b))
            elif fn == 45: r[rd] = u64(a + b)
            elif fn == 47: r[rd] = u64(a - b)
            elif fn == 56: r[rd] = u64(b << sa)
            elif fn == 58: r[rd] = u64(b) >> sa
            elif fn == 59: r[rd] = u64(sx(b, 64) >> sa)
            elif fn == 60: r[rd] = u64(b << (sa + 32))
            elif fn == 62: r[rd] = u64(b) >> (sa + 32)
            elif fn == 63: r[rd] = u64(sx(b, 64) >> (sa + 32))
            else: raise AssertionError(('SPECIAL', fn, hex(w)))
        elif op == 9: r[rt] = s32(r[rs] + imm)
        elif op == 25: r[rt] = u64(r[rs] + imm)
        elif op == 10: r[rt] = int(sx(r[rs], 64) < imm)
        elif op == 11: r[rt] = int(u64(r[rs]) < u64(imm))
        elif op == 12: r[rt] = r[rs] & (w & 0xFFFF)
        elif op == 13: r[rt] = r[rs] | (w & 0xFFFF)
        elif op == 14: r[rt] = r[rs] ^ (w & 0xFFFF)
        elif op == 15: r[rt] = s32((w & 0xFFFF) << 16)
        elif op == 28:  # MMI: only PADDUB rd, rs, $zero (a register copy) occurs here
            assert fn == 40 and sa == 24 and rt == 0, ('MMI', hex(w))
            r[rd] = r[rs]
        elif op == 32: r[rt] = u64(sx(self.load(ea, 1), 8))
        elif op == 33: r[rt] = u64(sx(self.load(ea, 2), 16))
        elif op == 35: r[rt] = s32(self.load(ea, 4))
        elif op == 36: r[rt] = self.load(ea, 1)
        elif op == 37: r[rt] = self.load(ea, 2)
        elif op == 55: r[rt] = self.load(ea, 8)
        elif op == 30: r[rt] = self.load(ea, 8)          # lq: low doubleword
        elif op == 40: self.save(ea, r[rt] & 0xFF, 1)
        elif op == 41: self.save(ea, r[rt] & 0xFFFF, 2)
        elif op == 43: self.save(ea, r[rt] & 0xFFFFFFFF, 4)
        elif op == 63: self.save(ea, u64(r[rt]), 8)
        elif op == 31: self.save(ea, u64(r[rt]), 8); self.save(ea + 8, 0, 8)
        elif op == 49: f[rt] = self.load(ea, 4)
        elif op == 57: self.save(ea, f[rt], 4)
        elif op == 17:
            if rs == 0: r[rt] = s32(f[rd])
            elif rs == 4: f[rd] = r[rt] & 0xFFFFFFFF
            elif rs == 20 and fn == 32: f[sa] = ee.ee_cvt_s_w(f[rd])
            elif rs == 16:
                x, y = f[rd], f[rt]
                if fn == 0: f[sa] = ee.ee_add(x, y)
                elif fn == 1: f[sa] = ee.ee_sub(x, y)
                elif fn == 2: f[sa] = ee.ee_mul(x, y)
                elif fn == 3: f[sa] = ee.ee_div(x, y)
                elif fn == 6: f[sa] = x
                elif fn == 7: f[sa] = ee.ee_neg(x)
                elif fn == 50: self.cond = ee.ee_c_eq(x, y)
                elif fn == 52: self.cond = ee.ee_c_lt(x, y)
                elif fn == 54: self.cond = ee.ee_c_le(x, y)
                else: raise AssertionError(('COP1.S', fn, hex(w)))
            else: raise AssertionError(('COP1', rs, fn, hex(w)))
        else: raise AssertionError(('opcode', op, hex(w)))
        r[0] = 0


class Clip(C.Structure):
    _fields_ = [('sector', C.c_int32), ('unread', C.c_int32), ('size', C.c_int32), ('loop', C.c_int32)]


BRIDGE = r'''
#include "game/em_stream_lanes_original.h"
#include <string.h>
static EmStreamLanes L;
static EmStreamLanesGlobals G;
static EmStreamLanesData D;
static int32_t status[48];
static EmStreamClip music[128], voice[256];
static int32_t ev[4096][5];
static int nev;
static int32_t rng_value, r13280, r12610, r12D18;
static int push(int k, int a, int b, int c, int d) {
    if (nev >= 4096) return -1;
    ev[nev][0] = k; ev[nev][1] = a; ev[nev][2] = b; ev[nev][3] = c; ev[nev][4] = d; nev++;
    return 0;
}
static int w_iop(void *x, int32_t c, int32_t a, int32_t b, int32_t d) {(void)x; return push(1, c, a, b, d);}
static int w_rng(void *x, int32_t *v) {(void)x; *v = rng_value; return push(2, 0, 0, 0, 0);}
static int w_fc280(void *x) {(void)x; return push(3, 0, 0, 0, 0);}
static int w_fbc50(void *x) {(void)x; return push(4, 0, 0, 0, 0);}
static int w_13280(void *x, int32_t a, int32_t *r) {(void)x; *r = r13280; return push(5, a, 0, 0, 0);}
static int w_12610(void *x, uint32_t s, uint32_t n, uint32_t a, const uint8_t m[3], int32_t *r) {
    (void)x; *r = r12610; return push(6, (int)s, (int)n, (int)a, m[0] | m[1] << 8 | m[2] << 16);}
static int w_12D18(void *x, int32_t a, int32_t *r) {(void)x; *r = r12D18; return push(7, a, 0, 0, 0);}
static int w_13478(void *x, int32_t a) {(void)x; return push(8, a, 0, 0, 0);}
static const unsigned lane_off[] = {0x00,0x01,0x02,0x03,0x04,0x08,0x14,0x18,0x20,0x24,0x28,0x2C,
                                    0x30,0x34,0x38,0x48,0x4C,0x50,0x54,0x58,0x5C};
static void *lane_ptr(EmStreamLane *r, int k, unsigned *size) {
    switch (k) {
#define F(i, name) case i: *size = sizeof r->name; return &r->name;
    F(0, state) F(1, half) F(2, last_half) F(3, load) F(4, voice) F(5, voice_mask) F(6, buffer)
    F(7, buffer_size) F(8, loop) F(9, loop_sector) F(10, end_sector) F(11, base_sector)
    F(12, read_sector) F(13, read_count) F(14, read_addr) F(15, remaining) F(16, duration)
    F(17, start_time) F(18, fade_step) F(19, volume) F(20, release)
#undef F
    }
    return 0;
}
static void xfer(uint8_t *region, int out) {
    EmStreamLanesState *s = &L.state;
    unsigned size;
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 21; k++) {
            void *p = lane_ptr(&s->lane[i], k, &size);
            if (out) memcpy(region + i * 0x60 + lane_off[k], p, size);
            else memcpy(p, region + i * 0x60 + lane_off[k], size);
        }
#define B(off, field) if (out) memcpy(region + off, &s->field, sizeof s->field); \
                      else memcpy(&s->field, region + off, sizeof s->field);
    B(0x124, voice_right) B(0x184, active) B(0x187, read_phase) B(0x188, read_lane)
    B(0x18B, mono) B(0x1A8, cue) B(0x1B8, music_sector) B(0x1BC, voice_sector)
#undef B
}
void setup(const EmStreamClip *m, uint32_t mc, const EmStreamClip *v, uint32_t vc) {
    memcpy(music, m, sizeof music[0] * mc); memcpy(voice, v, sizeof voice[0] * vc);
    D.music = music; D.music_count = mc; D.voice = voice; D.voice_count = vc;
    EmStreamLanesWorkers w = {0, w_iop, w_rng, w_fc280, w_fbc50, w_13280, w_12610, w_12D18, w_13478};
    em_stream_lanes_bind(&L, &D, &G, &w);
}
void set_results(int32_t rng, int32_t a, int32_t b, int32_t c) {
    rng_value = rng; r13280 = a; r12610 = b; r12D18 = c;}
void load(uint8_t *region, const uint8_t *ring, const uint8_t *gp, const int32_t *glob, const int32_t *st) {
    memset(&L.state, 0, sizeof L.state);
    xfer(region, 0);
    memcpy(L.state.ring, ring, 0x40);
    memcpy(&L.state.music_clip, gp, 4);
    L.state.ring_head = (int8_t)gp[4]; L.state.ring_tail = (int8_t)gp[8];
    G.d810E90 = (uint32_t)glob[0]; G.d8106C8 = glob[1]; G.d810D38 = glob[2];
    G.d810700 = (uint8_t)glob[3]; G.d8104E4 = (uint8_t)glob[4];
    G.d8106F4 = (uint8_t)glob[5]; G.d8106F5 = (uint8_t)glob[6];
    memcpy(status, st, sizeof status); G.d281880 = status;
    L.fault.code = 0; L.fault.address = 0; nev = 0;
}
void save(uint8_t *region, uint8_t *ring, uint8_t *gp, int32_t *glob) {
    xfer(region, 1);
    memcpy(ring, L.state.ring, 0x40);
    memcpy(gp, &L.state.music_clip, 4);
    gp[4] = (uint8_t)L.state.ring_head; gp[8] = (uint8_t)L.state.ring_tail;
    glob[0] = (int32_t)G.d810E90; glob[1] = G.d8106C8; glob[2] = G.d810D38; glob[3] = G.d810700;
    glob[4] = G.d8104E4; glob[5] = G.d8106F4; glob[6] = G.d8106F5;
}
int call(int fn, int a, int b, int c, int d) {
    switch (fn) {
    case 0x1F9CF0: return em_stream_lanes_001F9CF0(&L);
    case 0x1FA0D0: return em_stream_lanes_001FA0D0(&L);
    case 0x1FA330: return em_stream_lanes_001FA330(&L);
    case 0x1FA5F0: return em_stream_lanes_001FA5F0(&L);
    case 0x1FA570: return em_stream_lanes_001FA570(&L);
    case 0x1FA790: return em_stream_lanes_001FA790(&L, a, b);
    case 0x1FABF0: return em_stream_lanes_001FABF0(&L, a, b, c, d);
    case 0x1FAD70: return em_stream_lanes_001FAD70(&L, a, b, c);
    case 0x1FAAC0: return em_stream_lanes_001FAAC0(&L, a);
    case 0x1FAB50: return em_stream_lanes_001FAB50(&L);
    case 0x1FAB80: return em_stream_lanes_001FAB80(&L);
    case 0x1FABB0: return em_stream_lanes_001FABB0(&L);
    case 0x1FD470: return em_stream_lanes_001FD470(&L, a);
    case 0x1FAE70: return em_stream_lanes_001FAE70(&L, a);
    case 0x1FB0B0: return em_stream_lanes_001FB0B0(&L, a);
    case 0x119828: return em_stream_lanes_00119828(&L, a, b, c);
    }
    return -99;
}
int take(int32_t *out) {int n = nev; memcpy(out, ev, sizeof ev[0] * (size_t)n); nev = 0; return n;}
int fault_code(void) {return L.fault.code;}
unsigned fault_address(void) {return L.fault.address;}
'''


class Native:
    def __init__(self, out, music, voice):
        (out / 'bridge.c').write_text(BRIDGE)
        lib = out / 'stream_lanes.dylib'
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                        '-shared', '-fPIC', '-Isrc', 'src/game/em_stream_lanes_original.c',
                        str(out / 'bridge.c'), '-o', str(lib)], cwd=ROOT, check=True)
        self.lib = C.CDLL(str(lib))
        self.lib.em_stream_lanes_001281C0.restype = C.c_int32
        self.lib.em_stream_lanes_001281C0.argtypes = [C.c_uint32]
        self.lib.em_stream_lanes_00128250.restype = C.c_uint32
        self.lib.em_stream_lanes_00128250.argtypes = [C.c_uint32]
        self.lib.fault_address.restype = C.c_uint32
        self.setup(music, voice)
        self.events = (C.c_int32 * (4096 * 5))()

    def setup(self, music, voice):
        m = (Clip * len(music))(*[Clip(*row) for row in music])
        v = (Clip * len(voice))(*[Clip(*row) for row in voice])
        self.lib.setup(m, len(music), v, len(voice))


def rows(elf, base, count):
    offset = base - 0x100000 + 0x300
    return [struct.unpack_from('<4i', elf, offset + 16 * i) for i in range(count)]


class Case:
    """One original RAM state (capture + pokes) and its native twin."""

    def __init__(self, elf, capture, native, pokes=(), status=None, results=(0, 2, 1, 0)):
        self.o = EE(elf, capture)
        self.n = native
        for address, value, size in pokes:
            self.o.save(address, value & ((1 << (8 * size)) - 1), size, track=False)
        self.status = list(status) if status is not None else [0] * 48
        self.results = results
        self.events = []
        o = self.o
        record = lambda kind, n: (lambda e: self.events.append((kind, *[sx(e.r[4 + i]) for i in range(n)])))
        o.calls.update({
            0x1157F0: self.iop, 0x122BB8: self.rng, 0x1FC280: record(FC280, 0),
            0x1FBC50: record(FBC50, 0), 0x113280: self.cd(CD13280, 1), 0x112D18: self.cd(CD12D18, 3),
            0x113478: record(CD13478, 1), 0x112610: self.cd12610, 0x121A28: self.memset})
        self.push()

    # -- worker hooks (original side)
    def iop(self, e):
        self.events.append((IOP, *[sx(e.r[4 + i]) for i in range(4)])); e.r[2] = 0

    def rng(self, e):
        self.events.append((RNG, 0, 0, 0, 0)); e.r[2] = s32(self.results[0])

    def cd(self, kind, index):
        def call(e):
            self.events.append((kind, sx(e.r[4]), 0, 0, 0)); e.r[2] = s32(self.results[index])
        return call

    def cd12610(self, e):
        mode = e.read(e.r[7] & 0xFFFFFFFF, 3)
        self.events.append((CD12610, sx(e.r[4]), sx(e.r[5]), sx(e.r[6]), mode[0] | mode[1] << 8 | mode[2] << 16))
        e.r[2] = s32(self.results[2])

    def memset(self, e):
        assert (e.r[4] & 0xFFFFFFFF, e.r[5] & 0xFF, e.r[6] & 0xFFFFFFFF) == (RING, 0xFF, 0x40), 'memset args'
        for i in range(0x40): e.save(RING + i, 0xFF, 1)

    # -- state transfer
    def glob(self):
        return [sx(self.o.load(a, s, False), 8 * s) if s == 4 else self.o.load(a, s, False) for a, s in GLOBALS]

    def push(self):
        o, n = self.o, self.n
        for i, value in enumerate(self.status): o.save(STATUS + 4 * i, value & 0xFFFFFFFF, 4, track=False)
        region = (C.c_uint8 * REGION_SIZE)(*o.read(REGION, REGION_SIZE))
        ring = (C.c_uint8 * 0x40)(*o.read(RING, 0x40))
        gp = (C.c_uint8 * 12)(*o.read(GP0, 12))
        n.lib.load(region, ring, gp, (C.c_int32 * 7)(*self.glob()), (C.c_int32 * 48)(*self.status))
        n.lib.set_results(*[C.c_int32(sx(v)) for v in self.results])

    def set_status(self, status):
        self.status = list(status)
        for i, value in enumerate(self.status): self.o.save(STATUS + 4 * i, value & 0xFFFFFFFF, 4, track=False)
        region = (C.c_uint8 * REGION_SIZE)(); ring = (C.c_uint8 * 0x40)(); gp = (C.c_uint8 * 12)()
        glob = (C.c_int32 * 7)()
        self.n.lib.save(region, ring, gp, glob)       # keep native state, refresh the table
        self.n.lib.load(region, ring, gp, glob, (C.c_int32 * 48)(*self.status))

    def set_results(self, results):
        self.results = results
        self.n.lib.set_results(*[C.c_int32(sx(v)) for v in results])

    def poke_both(self, address, value, size):
        """Change an input byte range on both sides (e.g. a ring push)."""
        self.o.save(address, value & ((1 << (8 * size)) - 1), size, track=False)
        region = (C.c_uint8 * REGION_SIZE)(); ring = (C.c_uint8 * 0x40)(); gp = (C.c_uint8 * 12)()
        glob = (C.c_int32 * 7)()
        self.n.lib.save(region, ring, gp, glob)
        blob = {REGION: region, RING: ring, GP0: gp}
        for base, buf in blob.items():
            for i in range(size):
                if base <= address + i < base + len(buf):
                    buf[address + i - base] = value >> (8 * i) & 255
        for k, (a, s) in enumerate(GLOBALS):
            if a <= address < a + s:
                glob[k] = sx(value, 8 * s) if s == 4 else value & 0xFF
        self.n.lib.load(region, ring, gp, glob, (C.c_int32 * 48)(*self.status))

    def compare(self, label):
        o, n = self.o, self.n
        region = (C.c_uint8 * REGION_SIZE)(*o.read(REGION, REGION_SIZE))  # unmodelled bytes as the original has them
        # Native writes only modelled fields; start from the INPUT copy for unmodelled bytes.
        base = (C.c_uint8 * REGION_SIZE)(*self.input_region)
        ring = (C.c_uint8 * 0x40)(); gp = (C.c_uint8 * 12)(*self.input_gp); glob = (C.c_int32 * 7)()
        n.lib.save(base, ring, gp, glob)
        assert bytes(base) == bytes(region), dict(case=label, diff=[
            (hex(REGION + i), bytes(base)[i], bytes(region)[i]) for i in range(REGION_SIZE)
            if bytes(base)[i] != bytes(region)[i]][:12])
        assert bytes(ring) == o.read(RING, 0x40), (label, 'ring')
        assert bytes(gp) == o.read(GP0, 12), (label, 'gp', bytes(gp).hex(), o.read(GP0, 12).hex())
        assert list(glob) == self.glob(), (label, 'globals', list(glob), self.glob())

    def call(self, fn, args=(), label=None):
        o, n = self.o, self.n
        label = label or (hex(fn), args)
        self.input_region = o.read(REGION, REGION_SIZE)
        self.input_gp = o.read(GP0, 12)
        o.reads.clear(); o.writes.clear(); self.events = []
        o.run(fn, args)
        padded = list(args) + [0] * (4 - len(args))
        result = n.lib.call(fn, *[C.c_int(sx(v)) for v in padded])
        count = n.lib.take(n.events)
        native = [tuple(n.events[5 * i:5 * i + 5]) for i in range(count)]
        expected = [tuple(list(e) + [0] * (5 - len(e))) for e in self.events]
        assert n.lib.fault_code() == 0 and result == 0, (label, result, n.lib.fault_code(), hex(n.lib.fault_address()))
        assert native == expected, dict(case=label, native=native, original=expected)
        bad_reads = o.reads - READABLE
        assert not bad_reads, (label, 'unmodelled reads', sorted(hex(a) for a in bad_reads)[:12])
        assert o.writes <= WRITABLE, (label, 'unmodelled writes', sorted(hex(a) for a in o.writes - WRITABLE)[:12])
        self.compare(label)
        return expected


# ----------------------------------------------------------------- captures

def load_captures():
    base = DECOMP / 'build/startup-reference'
    names = ['opening_ee.bin', 'handoff_ee.bin', 'playable_ee.bin', 'elevator/completed_ee.bin',
             'elevator/clip47_ee.bin', 'elevator/refusal/eeMemory.bin', 'status-hub/eeMemory.bin',
             'roger-encounter/eeMemory.bin', 'panel/animation_ee.bin', 'panel/eeMemory.bin',
             'panel/root/eeMemory.bin']
    out = {}
    for name in names:
        if (base / name).exists(): out['ref/' + name] = (base / name).read_bytes()
    route = DECOMP / 'build/s87/route'
    if route.exists():
        for d in sorted(route.iterdir()):
            if (d / 'eeMemory.bin').exists(): out['route/' + d.name] = (d / 'eeMemory.bin').read_bytes()
    return out


def f32(value): return ee.f2b(value)


SPECIAL = [0, 0x80000000, 1, 0x80000001, 0x007FFFFF, 0x00800000, 0x80800000, 0x3F800000, 0xBF800000,
           0x3F7FFFFF, 0x3FFFFFFF, 0x4EFFFFFF, 0x4F000000, 0xCF000000, 0xCF000001, 0x4F7FFFFF,
           0x4F800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7FC00000, 0x7F900000,
           0xFFC00000, 0x467FFC00, 0x46400000, 0x3EFFFFFF, 0x3F000000, 0x4B000000, 0x4AFFFFFF]


def leaves(elf, capture, native, rng):
    values = SPECIAL + [rng.getrandbits(32) for _ in range(pick(20000, 800))]
    values += [f32(rng.uniform(-70000, 70000)) for _ in range(pick(5000, 200))]
    for value in values:
        o = EE(elf, capture)
        o.run(0x1281C0, f12=value)
        assert sx(o.r[2]) == native.lib.em_stream_lanes_001281C0(value), ('001281C0', hex(value))
        o.run(0x128250, f12=value)
        assert o.r[2] & 0xFFFFFFFF == native.lib.em_stream_lanes_00128250(value), ('00128250', hex(value))
    return len(values)


FADES = [0, 1, -1, 2, 0x40, 270, 326, 347, 397, 1000, 0x7FFFFFFF, -0x80000000, 3, 7, 0x1000001]


def fade_cases(elf, capture, native, rng):
    count = 0
    grid = [(lane, active, fade, enable, cue, release)
            for lane in range(3) for active in (0, 1, 2) for fade in FADES for enable in (0, 1)
            for cue in (0, 25, 29 if lane == 0 else 150) for release in (0, 1, 0x80, 0x1FF)]
    for lane, active, fade, enable, cue, release in select(grid, 160, 0x1FABF0, axes=(
            lambda c: c[0], lambda c: c[1], lambda c: c[2], lambda c: c[3], lambda c: c[4], lambda c: c[5])):
        pokes = [(0x282154 + lane, active, 1), (REGION + lane * 0x60 + 0x58, f32(9.5), 4)]
        case = Case(elf, capture, native, pokes)
        case.call(0x1FABF0, (lane, cue, fade, enable))
        case = Case(elf, capture, native, pokes)
        case.call(0x1FAD70, (lane, fade, release))
        count += 2
    return count


def volume_cases(elf, capture, native, rng):
    values = [0, 0x80000000, 1, f32(1.0), f32(-1.0), f32(16383.0), f32(16382.99), f32(12288.0),
              f32(12287.9), f32(50.2546), f32(-50.2546), f32(-16383.0), 0x7F800000, 0xFF800000,
              0x7FC00000, 0x7F7FFFFF, 0xFF7FFFFF, 0x00400000, 0x80400000, f32(3065.5244), f32(0.5)]
    grid = [(lane, step, vol, mono, release, voice)
            for lane in range(3) for step in values for vol in values[:12] for mono in (0, 1)
            for release in (0, 1) for voice in (0, 2, 63)]
    chosen = select(grid, pick(len(grid), 400), 0x1FA330, axes=(
        lambda c: c[0], lambda c: c[1], lambda c: c[2], lambda c: c[3], lambda c: c[4], lambda c: c[5]))
    for lane, step, vol, mono, release, voice in chosen:
        pokes = [(0x282154, 0, 1), (0x282155, 0, 1), (0x282156, 0, 1), (0x282154 + lane, 1, 1),
                 (0x28215B, mono, 1), (REGION + lane * 0x60 + 0x54, step, 4),
                 (REGION + lane * 0x60 + 0x58, vol, 4), (REGION + lane * 0x60 + 0x5C, release, 1),
                 (REGION + 4, voice, 4), (REGION + 0x124, voice ^ 1, 4)]
        Case(elf, capture, native, pokes).call(0x1FA330)
    for n in range(pick(3000, 120)):
        pokes = [(0x282154 + k, rng.choice((0, 1, 2)), 1) for k in range(3)]
        pokes += [(0x28215B, rng.choice((0, 1, 7)), 1), (REGION + 4, rng.randrange(64), 4),
                  (REGION + 0x124, rng.randrange(64), 4)]
        for k in range(3):
            pokes += [(REGION + k * 0x60 + 0x54, rng.choice((rng.getrandbits(32), f32(rng.uniform(-300, 300)))), 4),
                      (REGION + k * 0x60 + 0x58, f32(rng.uniform(-100, 17000)), 4),
                      (REGION + k * 0x60 + 0x5C, rng.randrange(256), 1)]
        Case(elf, capture, native, pokes).call(0x1FA330, label=('volume-random', n))
    return len(chosen) + pick(3000, 120)


def start_cases(elf, capture, native, rng):
    # Lane 0 reads D_0025DD30 + 16 * cue unbounded: cues past the 68 music
    # rows are the voice rows that follow (0x25DD30 + 16 * 68 = 0x25E170).
    grid = [(0, cue) for cue in range(0, MUSIC_ROWS + VOICE_ROWS)] + [
        (lane, cue) for lane in (1, 2) for cue in range(0, VOICE_ROWS)]
    keep = lambda i, c: c[1] in (0, 1, 0x18, 25, 29, 63) or (c[0] and 143 <= c[1] <= 151) or c[1] == \
        (MUSIC_ROWS - 1 if c[0] == 0 else VOICE_ROWS - 1) or (c[0] == 0 and c[1] in (
            MUSIC_ROWS, 127, MUSIC_ROWS + VOICE_ROWS - 1))
    chosen = select(grid, 90, 0x1FA790, axes=(lambda c: c[0],), keep=keep)
    for lane, cue in chosen:
        for active, size in ((0, None), (1, None), (0, 0x100000), (0, 0x1000)):
            pokes = [(0x282154 + lane, active, 1)]
            if size: pokes.append((REGION + lane * 0x60 + 0x18, size, 4))
            Case(elf, capture, native, pokes).call(0x1FA790, (lane, cue))
    return 4 * len(chosen)


def synthetic_row_cases(elf, capture, native, music, voice):
    """001FA790 over rows no ELF row reaches (sizes at the sign boundary),
    written identically into the original's table and the native view, so
    the sra/srl and signed-float arms are pinned too."""
    count = 0
    for size, buffer_size in itertools.product(
            (0x7FFFF900, 0x7FFFF7FF, 0x80000000, 0xFFFFFFFF, 0x7FF, 0x800, 0, 0x10001), (0x10000, 0xFFFFFFFE)):
        for lane, is_music, cue in ((0, True, 3), (1, False, 3), (0, False, MUSIC_ROWS + 3)):
            table, base = (music, MUSIC) if is_music else (voice, VOICE)
            rows_ = [list(r) for r in table]
            rows_[3][2] = sx(size)
            native.setup(rows_ if is_music else music, voice if is_music else rows_)
            case = Case(elf, capture, native, [(0x282154 + lane, 0, 1), (base + 16 * 3 + 8, size, 4),
                                               (REGION + lane * 0x60 + 0x18, buffer_size, 4)])
            case.call(0x1FA790, (lane, cue))
            count += 1
    native.setup(music, voice)
    return count


def select_cases(elf, capture, native, rng):
    grid = [(a0, c8, d38, area, e4, cue0, active0)
            for a0 in (0, 1) for c8 in (0x20081910, 0x20089910, 0x20080010, 0x2008C310)
            for d38 in (0, 0xB, 0xC, 0x17, 0x18, 25, 29, 0x99) for area in (0x0B, 0x15)
            for e4 in (0, 1, 2) for cue0 in (0, 0x18, 25, 29) for active0 in (0, 1, 2)]
    chosen = select(grid, 220, 0x1FAE70, axes=tuple((lambda k: (lambda c: c[k]))(k) for k in range(7)))
    for a0, c8, d38, area, e4, cue0, active0 in chosen:
        pokes = [(0x8106C8, c8, 4), (0x810D38, d38, 4), (0x810700, area, 1), (0x8104E4, e4, 1),
                 (0x282178, cue0, 4), (0x282154, active0, 1), (0x8106F4, 1, 1)]
        rngv = rng.getrandbits(31)
        Case(elf, capture, native, pokes, results=(rngv, 2, 1, 0)).call(0x1FAE70, (a0,))
        Case(elf, capture, native, pokes, results=(rngv, 2, 1, 0)).call(0x1FB0B0, (d38 ^ 0x19,))
    return 2 * len(chosen)


def stop_cases(elf, capture, native, rng):
    count = 0
    for act in ((0, 0, 0), (1, 1, 1), (2, 0, 2), (0, 2, 0)):
        pokes = [(0x282154 + k, act[k], 1) for k in range(3)] + [
            (0x8106F4, 1, 1), (0x8106F5, 1, 1), (0x282157, 2, 1), (0x275B30, 5, 1), (0x275B34, 3, 1),
            (RING + 12, 150, 4), (0x282178 + 4, 150, 4)]
        for fn, args in ((0x1FAAC0, (0,)), (0x1FAAC0, (1,)), (0x1FAAC0, (2,)), (0x1FAB50, ()),
                         (0x1FAB80, ()), (0x1FABB0, ()), (0x1FA570, ()), (0x1FD470, (0,)),
                         (0x1FD470, (1,)), (0x1FD470, (2,)), (0x1FD470, (3,)), (0x1FD470, (-1,)),
                         (0x119828, (0, 0x1999, 0x1999)), (0x119828, (1, -5, 0x7FFFFFFF))):
            Case(elf, capture, native, pokes).call(fn, args)
            count += 1
    return count


def read_cases(elf, capture, native, rng):
    grid = [(phase, lane, loads, res)
            for phase in (0, 1, 2, 3, -1) for lane in (0, 1, 2)
            for loads in ((0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1), (2, 1, 1), (1, 2, 1))
            for res in ((0, 2, 1, 0), (0, 1, 1, 0), (0, 2, 0, 1), (0, 2, 5, 3))]
    for phase, lane, loads, res in grid:
        pokes = [(0x282157, phase, 1), (0x282158, lane, 1)] + [
            (REGION + k * 0x60 + 3, loads[k], 1) for k in range(3)]
        Case(elf, capture, native, pokes, results=res).call(0x1FA0D0)
    return len(grid)


def ring_cases(elf, capture, native, rng):
    count = 0
    for tail in (0, 7, 15):
        for value in (-1, 0, 150, 149, 143, 151):
            for act in ((0, 0), (1, 0), (1, 1), (0, 1), (2, 2)):
                pokes = [(0x275B34, tail, 1), (RING + 4 * tail, value, 4),
                         (0x282155, act[0], 1), (0x282156, act[1], 1)]
                Case(elf, capture, native, pokes).call(0x1FA5F0)
                count += 1
    return count


SERVICE_GEOMETRY = dict(buffer=0xADC00, size=0x10000, loop=720927, end=722531)
# timing -> (start D_00282020, now D_00810E90, duration +0x4C). 'wrap': the
# counter wrapped and the original compares the raw counter (50 < 100)
# although the true elapsed time is 306. The last three pin the unsigned
# (sltu) timer compare: a duration or an elapsed value at or above 2**31.
SERVICE_TIMING = {'run': (1000, 1010, 100), 'expire': (1000, 1100, 100), 'wrap': (0xFFFFFF00, 50, 100),
                  'wrap-expire': (0xFFFFFFF0, 200, 100), 'long-duration': (1000, 1016, 0x90000000),
                  'huge-elapsed': (0x10, 0x90000010, 100), 'huge-wrap': (0xFFFFFFF0, 0x80000000, 100)}


def service_case(elf, capture, native, lane, active, state, load, half, last, loop, timing, addr, hold, status,
                 idle, sector=None, want_case=False):
    g = SERVICE_GEOMETRY
    base = REGION + lane * 0x60
    count = 16
    a1 = g['buffer'] + g['size'] if half == 1 else g['buffer'] + (g['size'] >> 1)
    read_addr = {'boundary': a1 - (count << 11), 'short': a1 - (count << 11) - 0x1000,
                 'long': g['buffer'] - 0x40000, 'sector-wrap': a1 - (count << 11)}[addr]
    end = g['loop'] + 2 if addr == 'long' else g['end']
    if sector is None:
        sector = end - 5 if addr == 'sector-wrap' else g['loop'] + 40
    start, now, duration = SERVICE_TIMING[timing]
    pokes = [(0x282154 + k, 0, 1) for k in range(3)] + [
        (0x282154 + lane, active, 1), (base, state, 1), (base + 1, half, 1), (base + 2, last, 1),
        (base + 3, load, 1), (base + 4, 5 + lane, 4), (base + 0x14, g['buffer'], 4),
        (base + 0x18, g['size'], 4), (base + 0x20, loop, 4), (base + 0x24, g['loop'], 4),
        (base + 0x28, end, 4), (base + 0x30, sector, 4), (base + 0x34, count, 4),
        (base + 0x38, read_addr, 4), (base + 0x4C, duration, 4), (base + 0x50, start, 4),
        (0x810E90, now, 4), (0x8106F4, hold, 1), (0x8106F5, hold, 1), (0x282157, 3, 1)]
    pokes += [(0x282178 + 4 * k, cue, 4) for k, cue in enumerate((0x19, 150, 149))]
    pokes += [(REGION + k * 0x60 + 4, 5 + k, 4) for k in range(3)]
    st = [idle] * 48; st[5 + lane] = status
    case = Case(elf, capture, native, pokes, status=st)
    events = case.call(0x1F9CF0, (0,))
    return case if want_case else events


def service_cases(elf, capture, native, rng):
    """001F9CF0 from constructed lane states: every arm of its switch, the
    timer end (with the counter wrap), the refill arms, the sector wrap and
    the hold bytes. Other lanes are idle."""
    grid = [(lane, active, state, load, half, last, loop, timing, addr, hold, status, idle)
            for lane in range(3) for active in (1, 2) for state in (0, 1, 2, 3) for load in (1, 2)
            for half in (1, 2) for last in (1, 2) for loop in (0, 1)
            for timing in SERVICE_TIMING
            for addr in ('boundary', 'short', 'long', 'sector-wrap') for hold in (0, 1)
            for status in (0, 0x40, 0x8000, 0xFFF0, 0x80000040, 0xFFFFFFF0) for idle in (0, 0x40)]
    axes = tuple((lambda k: (lambda c: c[k]))(k) for k in range(12))
    chosen = select(grid, 600, 0x1F9CF0, axes=axes + (lambda c: (c[2], c[3], c[8]), lambda c: (c[1], c[6], c[7])))
    for case in chosen:
        service_case(elf, capture, native, *case)
    return len(chosen) + unsigned_compare_cases(elf, capture, native) + sector_wrap_cases(elf, capture, native)


def sector_wrap_cases(elf, capture, native):
    """The end-of-clip sector wrap of 001F9CF0, pinned in every mode. When a
    half-buffer refill completes (state 1, load 2, the read address reaching
    the half's end), read_sector (+0x30) += read_count (+0x34), and an
    unsigned read_sector >= end_sector (+0x28) wraps to loop_sector (+0x24)
    + (read_sector - end_sector). Landing EXACTLY on end_sector must wrap to
    loop_sector itself (a '>' compare would leave it at end_sector); one
    sector short must not wrap; five past must wrap to loop_sector + 5."""
    g, count = SERVICE_GEOMETRY, 16
    loop, end = g['loop'], g['end']
    cases = ((end - count, loop), (end - count - 1, end - 1), (end - count + 5, loop + 5),
             (end - 1, loop + count - 1))
    n = 0
    for lane in range(3):
        for before, after in cases:
            case = service_case(elf, capture, native, lane, 1, 1, 2, 1, 2, 1, 'run', 'boundary', 0, 0x40, 0,
                                sector=before, want_case=True)
            base = REGION + lane * 0x60
            o = case.o
            # The refill completed: state and load back to 0, last_half = half.
            assert (o.load(base, 1, False), o.load(base + 3, 1, False), o.load(base + 2, 1, False)) == (0, 0, 1), \
                ('sector wrap: refill did not complete', lane, before)
            got = o.load(base + 0x30, 4, False)
            assert got == after, ('sector wrap', lane, before, got, after)
            n += 1   # case.call already proved the native lane bytes equal the original's
    return n


def unsigned_compare_cases(elf, capture, native):
    """The two sltu compares of 001F9CF0, pinned in every mode:
    - state 0: the 0011A730 status word against buffer_size >> 1 picks the
      half; a word at or above 2**31 is large unsigned (half 2) but negative
      signed (half 1). last_half 1 and 2 make both outcomes visible;
    - active 2, loop 0: elapsed against the +0x4C duration ends the clip; a
      duration or elapsed value at or above 2**31 separates sltu from slt."""
    count = 0
    for lane in range(3):
        for status in (0x80000040, 0xFFFFFFF0, 0x80000000, 0x7FFFFFFF):
            for last in (1, 2):
                service_case(elf, capture, native, lane, 1, 0, 1, 1, last, 1, 'run', 'boundary', 0, status, 0)
                count += 1
        for timing in ('long-duration', 'huge-elapsed', 'huge-wrap', 'run', 'expire'):
            for state in (1, 3):
                service_case(elf, capture, native, lane, 2, state, 1, 1, 1, 0, timing, 'boundary', 0, 0x40, 0)
                count += 1
    return count


def trajectories(elf, captures, native, rng, names, frames):
    """Lockstep 001F9CF0 frames from each captured state, with scripted IOP
    voice status and disc results, voice cues pushed into the ring and the
    Roger-style music calls injected on the way."""
    ticks = 0
    for name in names:
        capture = captures[name]
        seed = zlib.crc32(name.encode()) & 0xFFFF   # stable across processes (no str hash)
        SEEDS[name] = seed
        rs = random.Random(seed)
        case = Case(elf, capture, native, status=[0] * 48)
        heads = {}
        for t in range(frames):
            status = [0] * 48
            for k in range(3):
                voice = sx(case.o.load(REGION + k * 0x60 + 4, 4, False))
                if 0 <= voice < 48 and rs.random() < 0.9:
                    status[voice] = rs.choice((0x40, 0x7FF0, 0x8000, 0x8010, 0xFFF0, 0x100, 0x9000))
            case.set_status(status)
            case.set_results((rs.getrandbits(31), rs.choice((2, 2, 1)), rs.choice((1, 1, 0)), rs.choice((0, 0, 1))))
            case.poke_both(0x810E90, (case.o.load(0x810E90, 4, False) + 1) & 0xFFFFFFFF, 4)
            # Voice cues as 001FA5A0 pushes them: the Director's 150/149 first,
            # then the rest of the message-service harness range 143..151.
            pushes = {3: (150, 149), 40: (143, 144, 145), 70: (146, 147, 148, 151)}.get(t, ())
            if pushes:
                head = case.o.load(0x275B30, 1, False)
                for cue in pushes:
                    case.poke_both(RING + 4 * head, cue, 4); head = (head + 1) & 15
                case.poke_both(0x275B30, head, 1)
            # Hold bytes: another owner (the message service) sets 2, the lane
            # answers 1 when armed, the owner clears it to let the key-on go.
            if t in (5, 60, 100): case.poke_both(0x8106F5, 2, 1)
            for hold in (0x8106F4, 0x8106F5):
                if case.o.load(hold, 1, False) == 1 and rs.random() < 0.2: case.poke_both(hold, 0, 1)
            if t == 12: case.call(0x1FAD70, (0, 0x1E, 1), label=(name, t, 'fade'))
            if t == 14: case.call(0x1FB0B0, (29,), label=(name, t, 'bgm29'))
            if t == 20: case.call(0x1FAE70, (0,), label=(name, t, 'resume'))
            if t == 320: case.call(0x1FABB0, (), label=(name, t, 'stop'))
            if t == 321: case.call(0x1FAE70, (1,), label=(name, t, 'restart'))
            before = case.o.read(0x282155, 2)
            events = case.call(0x1F9CF0, (0,), label=(name, t))
            after = case.o.read(0x282155, 2)
            TRAJECTORY['voice_starts'] += sum(1 for k in range(2) if not before[k] and after[k])
            # an end may be followed by the next ring cue's start in the same call
            TRAJECTORY['voice_ends'] += sum(1 for k in range(2) if before[k] == 2 and after[k] != 2)
            TRAJECTORY['iop_0x42'] += sum(1 for e in events if e[0] == IOP and e[1] == 0x42)
            TRAJECTORY['iop_0x43'] += sum(1 for e in events if e[0] == IOP and e[1] == 0x43)
            TRAJECTORY['disc_reads'] += sum(1 for e in events if e[0] == CD12610)
            ticks += 1
    assert all(TRAJECTORY.values()), ('lockstep did not reach every lane transition', TRAJECTORY)
    return ticks


TRAJECTORY = dict(voice_starts=0, voice_ends=0, iop_0x42=0, iop_0x43=0, disc_reads=0)
SEEDS = {}  # lockstep capture -> scripted-input seed (printed on failure)


def capture_evidence(elf, captures, native, music, voice):
    """Captured lane fields reproduced by the native arithmetic (no oracle)."""
    checks = 0
    fades = set()
    for name, memory in captures.items():
        vs, ms = struct.unpack_from('<2I', memory, 0x282188)
        for lane in range(3):
            base = REGION + lane * 0x60
            r = dict(zip(('state', 'half', 'last', 'load'), memory[base:base + 4]))
            sector, end, loop_sector, duration = (struct.unpack_from('<I', memory, base + o)[0]
                                                  for o in (0x2C, 0x28, 0x24, 0x4C))
            if sector == 0: continue
            table, rows_ = (music, vs) if lane == 0 else (voice, ms)
            hits = [i for i, row in enumerate(table) if (rows_ + row[0]) & 0xFFFFFFFF == sector]
            assert hits, (name, lane, 'no clip row for the captured base sector')
            # 001FA790 over that row, natively, reproduces the captured fields.
            o = [(0x282154 + lane, 0, 1)]
            case = Case(elf, memory, native, o)
            case.n.lib.call(0x1FA790, lane, hits[0], 0, 0)
            region = (C.c_uint8 * REGION_SIZE)(); ring = (C.c_uint8 * 0x40)(); gp = (C.c_uint8 * 12)()
            glob = (C.c_int32 * 7)()
            case.n.lib.save(region, ring, gp, glob)
            got = lambda off: struct.unpack_from('<I', bytes(region), lane * 0x60 + off)[0]
            assert (got(0x4C), got(0x28), got(0x24), got(0x2C)) == (duration, end, loop_sector, sector), \
                (name, lane, hits[0])
            checks += 1
        # Lane-0 fade-in step and volume (001FAE70 fade 270 + seven RNG bits, 001FA330 adds).
        step, vol = struct.unpack_from('<2I', memory, REGION + 0x54)
        if step not in (0, f32(16383.0)) and memory[0x282154]:
            fits = [s2 for s2 in range(128) if ee.ee_div(0x467FFC00, ee.ee_cvt_s_w(270 + s2)) == step]
            assert len(fits) == 1, (name, 'fade step not 16383/(270+s2)', hex(step))
            acc, n = 0, 0
            while acc != vol and n < 400:
                acc, n = ee.ee_add(acc, step), n + 1
            assert acc == vol, (name, 'volume not a whole number of EE adds of the step')
            fades.add((name, 270 + fits[0], n))
            checks += 1
    return checks, sorted(fades)


def main():
    start = time.time()
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA, 'not the pinned boot ELF'
    captures = load_captures()
    assert captures, 'no captured RAM images'
    for name, memory in captures.items():
        for lo, size in EXECUTED:
            assert memory[lo:lo + size] == elf[lo - 0x100000 + 0x300:lo - 0x100000 + 0x300 + size], (name, hex(lo))
        for base, count in ((MUSIC, MUSIC_ROWS), (VOICE, VOICE_ROWS)):
            assert memory[base:base + 16 * count] == elf[base - 0x100000 + 0x300:base - 0x100000 + 0x300 + 16 * count]
    music, voice = rows(elf, MUSIC, MUSIC_ROWS), rows(elf, VOICE, VOICE_ROWS)
    out = ROOT / 'build/stream_lanes_reference'; out.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=f'bridge-{os.getpid()}-', dir=out))  # per process: no race on the dylib
    try:
        run(elf, captures, out, work, music, voice, start)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def run(elf, captures, out, work, music, voice, start):
    native = Native(work, music, voice)
    rng = random.Random(0x1F9CF0)
    base = captures.get('ref/roger-encounter/eeMemory.bin') or next(iter(captures.values()))
    assert struct.unpack_from('<i', base, 0x282178)[0] == 29, 'roger-encounter capture: lane 0 cue 29'
    counts, timing = {}, {}
    for key, family in (('leaves', leaves), ('fade', fade_cases), ('volume', volume_cases),
                        ('start', start_cases), ('select', select_cases), ('stop', stop_cases),
                        ('read', read_cases), ('ring', ring_cases), ('service', service_cases)):
        t0 = time.time()
        counts[key] = family(elf, base, native, rng)
        timing[key] = round(time.time() - t0, 2)
    names = sorted(captures)
    traj = select(names, 4, 0xF9CF0, keep=lambda i, n: n in (
        'ref/roger-encounter/eeMemory.bin', 'route/10_cage_roof_roger', 'route/12_crevice_jump',
        'ref/handoff_ee.bin'))
    frames = pick(700, 330)
    counts['synthetic_rows'] = synthetic_row_cases(elf, base, native, music, voice)
    t0 = time.time()
    try:
        counts['lockstep_ticks'] = trajectories(elf, captures, native, rng, traj, frames)
    except AssertionError:
        print('lockstep seeds (zlib.crc32(capture name) & 0xFFFF):',
              {k: hex(v) for k, v in SEEDS.items()}, file=sys.stderr)
        raise
    timing['lockstep'] = round(time.time() - t0, 2)
    evidence, fades = capture_evidence(elf, captures, native, music, voice)
    counts['capture_checks'] = evidence
    coverage = {}
    for lo, size in EXECUTED:
        reachable = [a for a in range(lo, lo + size, 4) if a not in UNREACHABLE]
        missed = [hex(a) for a in reachable if a not in COVERED]
        assert not missed, ('original instructions not exercised', missed)
        coverage[f'{lo:08X}'] = f'{len(reachable)}/{size // 4}'
    report = dict(status='PASS', elf_sha256=ELF_SHA, captures=len(captures), cases=counts,
                  captured_fades=[list(f) for f in fades], trajectory_captures=traj,
                  frames_per_trajectory=frames, lockstep_seeds=SEEDS, lockstep_transitions=TRAJECTORY,
                  seconds=timing,
                  instruction_coverage=coverage)
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    total = sum(v for k, v in counts.items() if k not in ('lockstep_ticks', 'capture_checks'))
    banner(f'{total:,} single-call cases', part(len(traj), len(names), 'lockstep captures'),
           f"{counts['lockstep_ticks']} lockstep 001F9CF0 frames")
    print(f"Original stream lanes: PASS {total} cases, {counts['lockstep_ticks']} lockstep frames, "
          f"{evidence} captured lane fields from {len(captures)} RAM images reproduced "
          f"({time.time() - start:.1f} s)")


if __name__ == '__main__':
    main()
