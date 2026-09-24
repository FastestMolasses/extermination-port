#!/usr/bin/env python3
"""Reference test for the IOP stream backend (src/game/em_iop_stream.c,
docs/IOP_STREAM.md).

1. Driver oracle. The ORIGINAL sound driver module (SNDN2DRV.IRX, read from
   the user's disc image and located in each captured IOP RAM, every
   non-relocated word checked) runs in a MIPS interpreter over captured
   original IOP RAM: the RPC 0x64 copy (0x12C), the command dispatcher (0x634)
   for the stream commands, the tick's command drain + status snapshot
   (0x328), the play-address scan (0x20C8), the transfer consumer (0x23B8)
   and the queue push (0x2CF4). The libsd and sysclib imports are replaced by
   recorders with scripted answers, identical on both sides. After every
   call the native translation must hold the same bytes in every modelled
   driver region and must have made the same libsd calls in the same order
   (transfers compared by a hash of the staged bytes). Every other original
   store must stay on the stack.
2. EE oracle. The original 0011A2B0, 001FA6A0, sub_cdrom0_IRX_SNDN2DRV_IRX_1
   (its tail allocations) and sub_O_STREAM_MUSIC_DAT_1 run over captured EE
   RAM with their workers scripted; results and stores are compared.
3. Capture evidence (native model against the 27 images; 16 of them also
   carry IOP RAM and SPU2 RAM in their save states):
   - boot buffers D_00275B50/28/4C/24/20 and D_00282188/8C;
   - each captured driver voice record's configuration against the native
     driver after the native 001F9820;
   - the lane-0 IOP buffer halves against the exported sectors the lane's
     read state says they hold;
   - the SPU2 RAM halves of the stream voices against the exported sectors:
     the de-interleaved channel, with the loop-flag bytes the model writes;
   - a field-by-field co-simulation of the lanes and the backend from each
     captured stream start: the cursor word (D_00281880) and the lane's read
     state at the captured elapsed field count (the IOP-side voice records
     are computed but not yet compared), the opening's stream request with its hold byte, the
     status page's stop/resume, and the voice lanes' end states;
   - the played samples of a long co-simulated stream against an independent
     decode of the exported cue (continuity over buffer wraps and the loop).

Quick mode samples the bulk sweeps; EM_TEST_FULL=1 runs them all.
"""
from __future__ import annotations

import ctypes as C
import hashlib
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from reference_mode import FULL, banner, part, pick, select  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
EMST = ROOT / 'assets/streams/streams.emst'
M32, M64 = 0xFFFFFFFF, (1 << 64) - 1
RETURN = 0x0BADF00C
IOP_STACK, EE_STACK = 0x1FF000, 0x01F00000

# Driver module offsets of the modelled state (docs/IOP_STREAM.md).
REGIONS = {'queue_write': (0x3494, 4), 'queue_read': (0x3498, 4), 'ring_write': (0x34A0, 4),
           'ring_read': (0x34A4, 4), 'pending': (0x34A8, 4), 'status': (0x44B0, 0x200),
           'staging': (0x46B0, 0x2000), 'ring': (0x66C0, 0x1000), 'voices': (0x76C0, 0x9C0),
           'queue': (0x8080, 0x600), 'records': (0x8680, 0x200)}
BSS_END = 0x8880
RPC_BUFFER = 0x34B0
STUBS = {0x314C: 'set_param', 0x3154: 'get_param', 0x315C: 'set_switch', 0x3164: 'set_addr',
         0x316C: 'get_addr', 0x317C: 'voice_trans', 0x318C: 'trans_status', 0x326C: 'memcpy',
         0x3274: 'memset'}
DRIVER_FUNCS = {'rpc': 0x12C, 'command': 0x634, 'tick': 0x328, 'scan': 0x20C8, 'consume': 0x23B8,
                'push': 0x2CF4}
# Translated driver code ranges (for coverage): RPC handler (fno 0x64 part),
# tick, dispatcher head/tail and the stream handlers, scan, consumer, push.
DRIVER_RANGES = ((0x12C, 0x2C8), (0x328, 0x544), (0x634, 0x698), (0xC94, 0xCF4), (0x16D4, 0x20C8),
                 (0x20C8, 0x23B8), (0x23B8, 0x2CF4), (0x2CF4, 0x2E1C))
# Driver words no oracle case reaches, each for a stated reason:
# 0x1E8: the ring copy's rounding of a negative remainder (the remainder is
#   always 1..0x1000); 0x288..0x2B0: the RPC function other than 0x64 (a
#   synchronous single command 001152D8 never uses; not translated);
# 0x209C/0x20A0: the dispatcher's default target (commands outside the table;
#   the port forwards them); 0x24C8, 0x24D8..0x24E0, 0x2600, 0x2610..0x2618,
#   0x2650, 0x2660..0x2668: the divide-by-zero / overflow traps (the port
#   faults instead); 0x2818/0x281C: dead code after an unconditional jump.
UNREACHED = {0x1E8, *range(0x288, 0x2B4, 4), 0x209C, 0x20A0, 0x24C8, 0x24D8, 0x24DC, 0x24E0, 0x2600, 0x2610,
             0x2614, 0x2618, 0x2650, 0x2660, 0x2664, 0x2668, 0x2818, 0x281C}
LIBSD_KIND = {'set_param': 5, 'get_param': 6, 'set_switch': 7, 'set_addr': 9, 'get_addr': 10,
              'voice_trans': 17, 'trans_status': 19}


def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def s32(b, o): return struct.unpack_from('<i', b, o)[0]


def sx(v, bits=32):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


def x64(v): return sx(v) & M64   # a 32-bit result, sign-extended into a 64-bit register


def fnv(data):
    h = 2166136261
    for byte in data:
        h = ((h ^ byte) * 16777619) & M32
    return h


# ----------------------------------------------------------------- inputs

def iso_file(iso: Path, name: str) -> tuple[int, bytes]:
    with iso.open('rb') as f:
        f.seek(16 * 2048)
        pvd = f.read(2048)
        assert pvd[:7] == b'\x01CD001\x01', 'not an ISO9660 image'
        rec = pvd[156:]
        extent, length = u32(rec, 2), u32(rec, 10)
        for comp in name.strip('/').split('/'):
            f.seek(extent * 2048)
            d = f.read(length)
            pos, found = 0, None
            while pos < len(d):
                size = d[pos]
                if not size:
                    pos = (pos // 2048 + 1) * 2048
                    continue
                item = d[pos:pos + size]
                if item[33:33 + item[32]].decode('ascii').split(';')[0] == comp:
                    found = item
                    break
                pos += size
            assert found is not None, (name, comp)
            extent, length = u32(found, 2), u32(found, 10)
        f.seek(extent * 2048)
        return extent, f.read(length)


def irx_sections(raw):
    table = u32(raw, 32)
    stride, count, names = struct.unpack_from('<3H', raw, 46)
    hs = [struct.unpack_from('<10I', raw, table + i * stride) for i in range(count)]
    strings = raw[hs[names][4]:hs[names][4] + hs[names][5]]
    return {strings[h[0]:].split(b'\0')[0].decode(): raw[h[4]:h[4] + h[5]] for h in hs}


def extract_states(names):
    """iopMemory.bin and SPU2.bin of each route save state, cached under build/."""
    cache = ROOT / 'build/iop_stream_reference/states'
    missing = [n for n in names if not (cache / n / 'SPU2.bin').exists()]
    if missing:
        code = ('import sys\nfrom pathlib import Path\nfrom tools.parse_pcsx2_state import extract_zstd_entry\n'
                'for n in sys.argv[2:]:\n'
                '    o = Path(sys.argv[1]) / n; o.mkdir(parents=True, exist_ok=True)\n'
                '    p = Path("build/s87/route") / n / "state.p2s"\n'
                '    for m in ("iopMemory.bin", "SPU2.bin"): (o / m).write_bytes(extract_zstd_entry(p, m))\n')
        subprocess.run([str(DECOMP / '.venv/bin/python'), '-c', code, str(cache), *missing], cwd=DECOMP, check=True)
    return {n: ((cache / n / 'iopMemory.bin').read_bytes(), (cache / n / 'SPU2.bin').read_bytes()) for n in names}


def load_captures():
    base = DECOMP / 'build/startup-reference'
    names = ['opening_ee.bin', 'handoff_ee.bin', 'playable_ee.bin', 'elevator/completed_ee.bin',
             'elevator/clip47_ee.bin', 'elevator/refusal/eeMemory.bin', 'status-hub/eeMemory.bin',
             'roger-encounter/eeMemory.bin', 'panel/animation_ee.bin', 'panel/eeMemory.bin',
             'panel/root/eeMemory.bin']
    out = {'ref/' + n: (base / n).read_bytes() for n in names if (base / n).exists()}
    route = DECOMP / 'build/s87/route'
    routes = sorted(d.name for d in route.iterdir() if (d / 'eeMemory.bin').exists() and (d / 'state.p2s').exists())
    for n in routes:
        out['route/' + n] = (route / n / 'eeMemory.bin').read_bytes()
    return out, routes


# ----------------------------------------------------------------- interpreter

class Trap(Exception):
    pass


LOADS = {32: 1, 33: 2, 35: 4, 36: 1, 37: 2, 55: 8, 30: 8}


def reads_of(w):
    """Registers an instruction reads (for the IOP load-delay guard)."""
    op, rs, rt, fn = w >> 26, w >> 21 & 31, w >> 16 & 31, w & 63
    if w == 0:
        return set()
    if op == 0:
        if fn in (0, 2, 3):
            return {rt}
        if fn in (8, 9):
            return {rs}
        if fn in (16, 18):
            return set()
        return {rs, rt}
    if op in (2, 3, 15):
        return set()
    if op in (4, 5, 20, 21) or op >= 40:
        return {rs, rt}
    return {rs}


class Cpu:
    """Bounded MIPS interpreter over a RAM image with a write overlay. `ee`
    selects 64-bit EE semantics for the few EE-only operations; IOP code is
    checked for load-delay hazards (the driver is -O0 code that never has
    one; a hazard would make the interpretation ambiguous)."""

    def __init__(self, ram, fetch, *, ee, stack):
        self.ram, self.fetch, self.ee = ram, fetch, ee
        self.mask = 0x1FFFFFF if ee else 0x1FFFFF
        self.direct = isinstance(ram, bytearray)   # a private copy: written in place
        self.over = {}
        self.r = [0] * 32
        self.hi = self.lo = 0
        self.hooks = {}
        self.stack = stack
        self.reads, self.writes = set(), set()
        self.covered = set()

    def byte(self, a):
        a &= self.mask
        if self.direct:
            return self.ram[a]
        return self.over[a] if a in self.over else self.ram[a]

    def untracked(self, a): return self.stack - 0x2000 <= a < self.stack + 0x100

    def load(self, a, n, track=True):
        a &= self.mask
        if track and not self.untracked(a):
            self.reads.update(range(a, a + n))
        if self.direct:
            return int.from_bytes(self.ram[a:a + n], 'little')
        return int.from_bytes(bytes(self.byte(a + i) for i in range(n)), 'little')

    def save(self, a, v, n, track=True):
        a &= self.mask
        if track and not self.untracked(a):
            self.writes.update(range(a, a + n))
        if self.direct:
            self.ram[a:a + n] = (v & ((1 << (8 * n)) - 1)).to_bytes(n, 'little')
            return
        for i in range(n):
            self.over[a + i] = (v >> (8 * i)) & 255

    def read(self, a, n):
        a &= self.mask
        if self.direct:
            return bytes(self.ram[a:a + n])
        return bytes(self.byte(a + i) for i in range(n))

    def cstr(self, a):
        out = bytearray()
        while self.byte(a + len(out)):
            out.append(self.byte(a + len(out)))
        return bytes(out)

    def write(self, a, data, track=False):
        a &= self.mask
        if track and not self.untracked(a):
            self.writes.update(range(a, a + len(data)))
        if self.direct:
            self.ram[a:a + len(data)] = data
            return
        for i, b in enumerate(data):
            self.over[a + i] = b

    def call(self, entry, args=(), stack_args=()):
        r = self.r = [0] * 32
        r[29], r[31] = self.stack, RETURN
        r[28] = 0x27D370 if self.ee else 0
        for i, v in enumerate(args):
            r[4 + i] = x64(v)
        for i, v in enumerate(stack_args):
            self.save(self.stack + 0x10 + 4 * i, v & M32, 4, track=False)
        pc, steps = entry, 0
        while pc != RETURN:
            steps += 1
            if steps > 2_000_000:
                raise AssertionError(('did not return', hex(entry)))
            if pc in self.hooks:
                self.hooks[pc](self)
                pc = r[31] & M32
                continue
            w = self.fetch(pc)
            self.covered.add(pc)
            op, rs, rt = w >> 26, w >> 21 & 31, w >> 16 & 31
            imm = sx(w & 0xFFFF, 16)
            if op in (2, 3) or (op == 0 and w & 63 in (8, 9)):
                if op in (2, 3):
                    dest = (pc & 0xF0000000) | (w & 0x3FFFFFF) << 2
                    link = op == 3
                else:
                    dest = r[rs] & M32
                    link = w & 63 == 9
                if link:
                    r[31 if op == 3 else w >> 11 & 31] = pc + 8
                self.step(self.fetch(pc + 4))
                self.covered.add(pc + 4)
                if dest in self.hooks:
                    self.hooks[dest](self)
                    pc = pc + 8 if link else r[31] & M32
                else:
                    pc = dest
                continue
            likely, taken = False, None
            if op in (4, 5, 20, 21):
                taken = (r[rs] & M64) == (r[rt] & M64) if self.ee else (r[rs] & M32) == (r[rt] & M32)
                taken = taken == (op in (4, 20))
                likely = op >= 20
            elif op in (6, 7, 22, 23):
                v = sx(r[rs], 64 if self.ee else 32)
                taken = v <= 0 if op in (6, 22) else v > 0
                likely = op >= 22
            elif op == 1:
                assert rt in (0, 1, 2, 3), hex(w)
                v = sx(r[rs], 64 if self.ee else 32)
                taken = v < 0 if rt in (0, 2) else v >= 0
                likely = rt >= 2
            if taken is None:
                self.step(w)
                if not self.ee and op in LOADS and rt:
                    assert rt not in reads_of(self.fetch(pc + 4)), ('load delay hazard', hex(pc))
                pc += 4
                continue
            if taken:
                self.step(self.fetch(pc + 4))
                self.covered.add(pc + 4)
                pc = pc + 4 + imm * 4
            elif likely:
                pc += 8
            else:
                self.step(self.fetch(pc + 4))
                self.covered.add(pc + 4)
                pc += 8
        return r[2]

    def step(self, w):
        r = self.r
        op, rs, rt, rd, sa, fn = w >> 26, w >> 21 & 31, w >> 16 & 31, w >> 11 & 31, w >> 6 & 31, w & 63
        imm = sx(w & 0xFFFF, 16)
        ea = (r[rs] + imm) & M32
        a, b = r[rs], r[rt]
        if op == 0:
            if fn == 0: r[rd] = x64(b << sa)
            elif fn == 2: r[rd] = x64((b & M32) >> sa)
            elif fn == 3: r[rd] = x64(sx(b) >> sa)
            elif fn == 4: r[rd] = x64(b << (a & 31))
            elif fn == 6: r[rd] = x64((b & M32) >> (a & 31))
            elif fn == 7: r[rd] = x64(sx(b) >> (a & 31))
            elif fn == 13: raise Trap(('break', w >> 6 & 0xFFFFF))
            elif fn == 16: r[rd] = self.hi
            elif fn == 18: r[rd] = self.lo
            elif fn in (24, 25):
                p = sx(a) * sx(b) if fn == 24 else (a & M32) * (b & M32)
                self.lo, self.hi = x64(p), x64(p >> 32)
                if self.ee and rd:
                    r[rd] = self.lo
            elif fn in (26, 27):
                if fn == 26:
                    n, d = sx(a), sx(b)
                    if d != 0 and not (n == -0x80000000 and d == -1):
                        q = abs(n) // abs(d) * (1 if (n < 0) == (d < 0) else -1)
                        self.lo, self.hi = x64(q), x64(n - q * d)
                else:
                    n, d = a & M32, b & M32
                    if d:
                        self.lo, self.hi = x64(n // d), x64(n % d)
            elif fn in (32, 33): r[rd] = x64(a + b)
            elif fn in (34, 35): r[rd] = x64(a - b)
            elif fn == 36: r[rd] = a & b
            elif fn == 37: r[rd] = a | b
            elif fn == 38: r[rd] = a ^ b
            elif fn == 39: r[rd] = ~(a | b) & M64
            elif fn == 42: r[rd] = int(sx(a, 64 if self.ee else 32) < sx(b, 64 if self.ee else 32))
            elif fn == 43: r[rd] = int((a & (M64 if self.ee else M32)) < (b & (M64 if self.ee else M32)))
            elif fn == 45: r[rd] = (a + b) & M64
            elif fn == 20: r[rd] = (b << (a & 63)) & M64
            elif fn == 56: r[rd] = (b << sa) & M64
            elif fn == 60: r[rd] = (b << (sa + 32)) & M64
            elif fn == 58: r[rd] = (b & M64) >> sa
            elif fn == 62: r[rd] = (b & M64) >> (sa + 32)
            elif fn == 63: r[rd] = (sx(b, 64) >> (sa + 32)) & M64
            else: raise AssertionError(('SPECIAL', fn, hex(w)))
        elif op in (8, 9): r[rt] = x64(a + imm)
        elif op == 25: r[rt] = (a + imm) & M64
        elif op == 10: r[rt] = int(sx(a, 64 if self.ee else 32) < imm)
        elif op == 11: r[rt] = int((a & (M64 if self.ee else M32)) < (imm & (M64 if self.ee else M32)))
        elif op == 12: r[rt] = a & (w & 0xFFFF)
        elif op == 13: r[rt] = a | (w & 0xFFFF)
        elif op == 14: r[rt] = a ^ (w & 0xFFFF)
        elif op == 15: r[rt] = x64((w & 0xFFFF) << 16)
        elif op == 28:   # MMI: the register-copy form only (rd = rs)
            assert fn == 40 and sa == 24 and rt == 0, ('MMI', hex(w))
            r[rd] = r[rs]
        elif op == 32: r[rt] = sx(self.load(ea, 1), 8) & M64
        elif op == 33: r[rt] = sx(self.load(ea, 2), 16) & M64
        elif op == 35: r[rt] = x64(self.load(ea, 4))
        elif op == 36: r[rt] = self.load(ea, 1)
        elif op == 37: r[rt] = self.load(ea, 2)
        elif op in (55, 30): r[rt] = self.load(ea, 8)
        elif op == 40: self.save(ea, a if False else b & 0xFF, 1)
        elif op == 41: self.save(ea, b & 0xFFFF, 2)
        elif op == 43: self.save(ea, b & M32, 4)
        elif op == 63: self.save(ea, b & M64, 8)
        elif op == 31: self.save(ea, b & M64, 8); self.save(ea + 8, 0, 8)
        else: raise AssertionError(('opcode', op, hex(w)))
        r[0] = 0


# ----------------------------------------------------------------- native bridge

BRIDGE = r'''
#include "game/em_iop_stream.h"
#include <stdlib.h>
#include <string.h>

/* ---- driver oracle instance ---- */
static EmIopStream *S;
static uint32_t lg[16384][6];
static int nlg;
static uint32_t nax_s[48];
static uint16_t envx_s[48];
static int32_t tst;
static uint32_t fnv(const uint8_t *p, uint32_t n)
{ uint32_t h = 2166136261u; while (n--) { h ^= *p++; h *= 16777619u; } return h; }
static void rec(uint32_t k, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{ if (nlg < 16384) { uint32_t *x = lg[nlg]; x[0] = k; x[1] = a; x[2] = b; x[3] = c; x[4] = d; x[5] = e; } nlg++; }
static int vi(uint16_t reg) { return ((reg & 1) * 24 + ((reg >> 1) & 0x1F)) % 48; }
static void r_sp(void *c, uint16_t reg, uint16_t v) { (void)c; rec(5, reg, v, 0, 0, 0); }
static uint16_t r_gp(void *c, uint16_t reg) { (void)c; rec(6, reg, 0, 0, 0, 0); return envx_s[vi(reg)]; }
static void r_ss(void *c, uint16_t reg, uint32_t v) { (void)c; rec(7, reg, v, 0, 0, 0); }
static void r_sa(void *c, uint16_t reg, uint32_t v) { (void)c; rec(9, reg, v, 0, 0, 0); }
static uint32_t r_ga(void *c, uint16_t reg) { (void)c; rec(10, reg, 0, 0, 0, 0); return nax_s[vi(reg)]; }
static void r_vt(void *c, int16_t ch, uint16_t mode, const uint8_t *d, uint32_t spu, uint32_t size)
{
    (void)c;
    rec(17, (uint16_t)ch, mode, spu, size, d == em_iop_stream_driver(S)->staging ? fnv(d, size) : 0xDEADBEEFu);
}
static int32_t r_ts(void *c, int16_t ch, int16_t fl) { (void)c; rec(19, (uint16_t)ch, (uint16_t)fl, 0, 0, 0); return tst; }
static void r_fw(void *c, const uint32_t cmd[4]) { (void)c; rec(99, cmd[0], cmd[1], cmd[2], cmd[3], 0); }

int drv_open(const uint8_t *iop)
{
    EmIopLibsd sd = {0, r_sp, r_gp, r_ss, r_sa, r_ga, r_vt, r_ts};
    if (!S && !(S = em_iop_stream_create())) return -1;
    memcpy(em_iop_stream_iop_ram(S), iop, EM_IOP_RAM_SIZE);
    em_iop_stream_set_libsd(S, &sd);
    em_iop_stream_set_forward(S, r_fw, 0);
    return 0;
}
void drv_script(const uint32_t *nax, const uint16_t *envx, int32_t status)
{ memcpy(nax_s, nax, sizeof nax_s); memcpy(envx_s, envx, sizeof envx_s); tst = status; }
#define PUT(off, src, n) memcpy(img + (off), (src), (n))
#define GET(off, dst, n) memcpy((dst), img + (off), (n))
void drv_save(uint8_t *img)
{
    EmIopDriver *d = em_iop_stream_driver(S);
    PUT(0x3494, &d->queue_write, 4); PUT(0x3498, &d->queue_read, 4); PUT(0x34A0, &d->ring_write, 4);
    PUT(0x34A4, &d->ring_read, 4); PUT(0x34A8, &d->pending, 4); PUT(0x44B0, d->status, 0x200);
    PUT(0x46B0, d->staging, 0x2000); PUT(0x66C0, d->ring, 0x1000); PUT(0x76C0, d->voice, 0x9C0);
    PUT(0x8080, d->queue, 0x600); PUT(0x8680, d->records, 0x200);
}
void drv_load(const uint8_t *img)
{
    EmIopDriver *d = em_iop_stream_driver(S);
    GET(0x3494, &d->queue_write, 4); GET(0x3498, &d->queue_read, 4); GET(0x34A0, &d->ring_write, 4);
    GET(0x34A4, &d->ring_read, 4); GET(0x34A8, &d->pending, 4); GET(0x44B0, d->status, 0x200);
    GET(0x46B0, d->staging, 0x2000); GET(0x66C0, d->ring, 0x1000); GET(0x76C0, d->voice, 0x9C0);
    GET(0x8080, d->queue, 0x600); GET(0x8680, d->records, 0x200);
}
int drv_call(int fn, const uint32_t *a, uint32_t n)
{
    nlg = 0;
    switch (fn) {
    case 0x12C: return em_iop_stream_drv_rpc(S, a, n);
    case 0x634: return em_iop_stream_drv_command(S, a);
    case 0x328: return em_iop_stream_drv_tick_commands(S);
    case 0x20C8: return em_iop_stream_drv_scan(S);
    case 0x23B8: return em_iop_stream_drv_consume(S);
    case 0x2CF4: return em_iop_stream_drv_push(S, a[0], a[1], a[2], a[3]);
    }
    return -99;
}
int drv_log(uint32_t *out) { int n = nlg < 16384 ? nlg : 16384; memcpy(out, lg, sizeof lg[0] * (size_t)n); return nlg; }
int drv_fault(void) { return em_iop_stream_fault(S)->code; }

/* ---- EE functions ---- */
static EmIopStream *E;
static EmIopStreamDisc EDISC;
int ee_open(void) { if (!E && !(E = em_iop_stream_create())) return -1; return 0; }
void ee_reset(void) { em_iop_stream_destroy(E); E = em_iop_stream_create(); }
uint32_t ee_001FA6A0(uint32_t next, int32_t size, uint32_t *after)
{ uint32_t v; em_iop_stream_set_heap_next(E, next); v = em_iop_stream_001FA6A0(E, size); *after = em_iop_stream_heap_next(E); return v; }
int ee_boot(uint32_t *out) { return em_iop_stream_boot_buffers(E, out); }
int ee_0011A2B0(uint8_t *table, uint64_t *mask, int32_t a0, int32_t *voice, uint32_t *queued, uint32_t *count)
{
    EmIopVoiceTable t = {table, mask};
    const uint32_t (*q)[4];
    int r;
    em_iop_stream_set_voice_table(E, &t);
    r = em_iop_stream_0011A2B0(E, a0, voice);
    q = em_iop_stream_ee_queue(E, count);
    if (*count) memcpy(queued, q[*count - 1], 16);
    return r;
}
int ee_sub_O(const char *m, const char *v, const char *pm, const char *pv, uint32_t lm, uint32_t lv, uint32_t *out)
{
    memset(&EDISC, 0, sizeof EDISC);
    strcpy(EDISC.search[0], m); strcpy(EDISC.search[1], v);
    strcpy(EDISC.path[0], pm); strcpy(EDISC.path[1], pv);
    EDISC.lsn[0] = lm; EDISC.lsn[1] = lv;
    em_iop_stream_attach_disc(E, &EDISC);
    return em_iop_stream_sub_O_STREAM_MUSIC_DAT_1(E, &out[0], &out[1]);
}

/* ---- co-simulation: lanes + backend ---- */
typedef struct { EmIopStream *iop; uint32_t lcg; } Sim;
static Sim SM;
static EmStreamLanes L;
static EmStreamLanesGlobals G;
static EmStreamLanesData LD;
static EmIopStreamDisc DISC;
static int disc_loaded;
static uint8_t VT[0x30 * 0x6A];
static uint64_t VM;
static int16_t *tap_buf[2];
static uint32_t tap_n[2], tap_cap;
static int tap_voice[2];
static uint32_t buffers[5];
static int w_rng(void *c, int32_t *v) { Sim *x = c; x->lcg = x->lcg * 1103515245u + 12345u; *v = (int32_t)((x->lcg >> 1) & 0x7FFFFFFF); return 0; }
static int w_none(void *c) { (void)c; return 0; }
static void tap(void *c, int voice, int16_t s)
{
    int k;
    (void)c;
    for (k = 0; k < 2; k++)
        if (voice == tap_voice[k] && tap_n[k] < tap_cap) tap_buf[k][tap_n[k]++] = s;
}
int sim_open(const char *path, uint32_t latency, uint32_t seed)
{
    EmStreamLanesWorkers w;
    if (!disc_loaded) { if (em_iop_stream_disc_load(&DISC, path)) return -1; disc_loaded = 1; }
    em_iop_stream_destroy(SM.iop);
    memset(&SM, 0, sizeof SM); memset(&L, 0, sizeof L); memset(&G, 0, sizeof G); memset(&w, 0, sizeof w);
    memset(VT, 0, sizeof VT); VM = 0;
    if (!(SM.iop = em_iop_stream_create())) return -1;
    SM.lcg = seed;
    em_iop_stream_attach_disc(SM.iop, &DISC);
    em_iop_stream_set_disc_latency(SM.iop, latency);
    { EmIopVoiceTable t = {VT, &VM}; em_iop_stream_set_voice_table(SM.iop, &t); }
    em_iop_stream_set_tap(SM.iop, tap, 0);
    if (em_iop_stream_boot_buffers(SM.iop, buffers)) return -2;
    G.d275B28 = buffers[1]; G.d275B24 = buffers[3]; G.d275B20 = buffers[4];
    G.d281880 = em_iop_stream_ee_status(SM.iop) + 48;
    if (em_iop_stream_sub_O_STREAM_MUSIC_DAT_1(SM.iop, &L.state.music_sector, &L.state.voice_sector)) return -3;
    em_iop_stream_lanes_data(&DISC, &LD);
    w.ctx = &SM; w.w_00122BB8 = w_rng; w.w_001FC280 = w_none; w.w_001FBC50 = w_none;
    em_iop_stream_lane_workers(SM.iop, &w);
    em_stream_lanes_bind(&L, &LD, &G, &w);
    if (em_stream_lanes_001F9820(&L)) return -4;
    return 0;
}
void sim_buffers(uint32_t *out) { memcpy(out, buffers, sizeof buffers); }
int sim_field(int service)
{
    G.d810E90++;
    if (em_iop_stream_field(SM.iop)) return -1;
    if (service && em_stream_lanes_001F9CF0(&L)) return -2;
    return 0;
}
int sim_call(int fn, int32_t a, int32_t b, int32_t c, int32_t d)
{
    switch (fn) {
    case 0x1FA790: return em_stream_lanes_001FA790(&L, a, b);
    case 0x1FABF0: return em_stream_lanes_001FABF0(&L, a, b, c, d);
    case 0x1FABB0: return em_stream_lanes_001FABB0(&L);
    case 0x1FAE70: return em_stream_lanes_001FAE70(&L, a);
    case 0x1FAD70: return em_stream_lanes_001FAD70(&L, a, b, c);
    case 0x1FAAC0: return em_stream_lanes_001FAAC0(&L, a);
    case 0x119828: return em_stream_lanes_00119828(&L, a, b, c);
    }
    return -99;
}
void sim_globals(int set, int32_t *g)
{
    if (set) {
        G.d810E90 = (uint32_t)g[0]; G.d8106C8 = g[1]; G.d810D38 = g[2]; G.d810700 = (uint8_t)g[3];
        G.d8104E4 = (uint8_t)g[4]; G.d8106F4 = (uint8_t)g[5]; G.d8106F5 = (uint8_t)g[6];
    }
    g[0] = (int32_t)G.d810E90; g[1] = G.d8106C8; g[2] = G.d810D38; g[3] = G.d810700;
    g[4] = G.d8104E4; g[5] = G.d8106F4; g[6] = G.d8106F5;
}
void sim_lane(int i, int32_t *o)
{
    EmStreamLane *r = &L.state.lane[i];
    o[0] = r->state; o[1] = r->half; o[2] = r->last_half; o[3] = r->load; o[4] = r->voice;
    o[5] = (int32_t)r->buffer; o[6] = (int32_t)r->buffer_size; o[7] = r->loop; o[8] = (int32_t)r->loop_sector;
    o[9] = (int32_t)r->end_sector; o[10] = (int32_t)r->base_sector; o[11] = (int32_t)r->read_sector;
    o[12] = (int32_t)r->read_count; o[13] = (int32_t)r->read_addr; o[14] = r->remaining;
    o[15] = (int32_t)r->duration; o[16] = (int32_t)r->start_time; o[17] = (int32_t)r->fade_step;
    o[18] = (int32_t)r->volume; o[19] = r->release; o[20] = L.state.active[i]; o[21] = L.state.cue[i];
    o[22] = L.state.read_phase; o[23] = L.state.read_lane; o[24] = L.state.voice_right;
    o[25] = (int32_t)L.state.music_sector; o[26] = (int32_t)L.state.voice_sector;
}
void sim_status(int32_t *ee, uint32_t *drv)
{
    memcpy(ee, em_iop_stream_ee_status(SM.iop), 0x200);
    memcpy(drv, em_iop_stream_driver(SM.iop)->status, 0x200);
}
void sim_voice(int v, uint32_t *o) { memcpy(o, &em_iop_stream_driver(SM.iop)->voice[v], 0x34); }
void sim_spu(int v, uint32_t *o)
{
    EmIopSpuVoice *p = em_iop_stream_spu_voice(SM.iop, v);
    o[0] = p->nax; o[1] = p->lsa; o[2] = p->on; o[3] = (uint32_t)p->env.level; o[4] = p->flags; o[5] = p->pos;
    o[6] = p->vol[0]; o[7] = p->vol[1]; o[8] = p->pitch; o[9] = p->ssa;
}
void sim_ram(int spu, uint32_t addr, uint32_t n, uint8_t *out)
{ memcpy(out, (spu ? em_iop_stream_spu_ram(SM.iop) : em_iop_stream_iop_ram(SM.iop)) + addr, n); }
int sim_fault(uint32_t *addr)
{
    if (L.fault.code) { *addr = L.fault.address; return 0x100 | L.fault.code; }
    *addr = em_iop_stream_fault(SM.iop)->address;
    return em_iop_stream_fault(SM.iop)->code;
}
void sim_tap(int16_t *a, int16_t *b, uint32_t cap, int va, int vb)
{ tap_buf[0] = a; tap_buf[1] = b; tap_cap = cap; tap_n[0] = tap_n[1] = 0; tap_voice[0] = va; tap_voice[1] = vb; }
void sim_tap_count(uint32_t *o) { o[0] = tap_n[0]; o[1] = tap_n[1]; }
uint64_t sim_digest(void) { return em_iop_stream_pcm_digest(SM.iop); }
'''


class Native:
    def __init__(self, work):
        (work / 'bridge.c').write_text(BRIDGE)
        lib = work / 'iop_stream.dylib'
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                        '-fPIC', '-Isrc', 'src/game/em_iop_stream.c', 'src/game/em_stream_lanes_original.c',
                        'src/game/em_sfx_bank.c', str(work / 'bridge.c'), '-o', str(lib)], cwd=ROOT, check=True)
        self.lib = C.CDLL(str(lib))
        self.lib.ee_001FA6A0.restype = C.c_uint32
        self.lib.sim_digest.restype = C.c_uint64
        self.log = (C.c_uint32 * (16384 * 6))()


# ----------------------------------------------------------------- driver oracle

class DriverOracle:
    """The original module over one captured IOP RAM image."""

    def __init__(self, irx, iop):
        sections = irx_sections(irx)
        text = sections['.text']
        rel = sections['.rel.text']
        match = iop.find(text[0x634:0x64C])
        assert match >= 0 and iop.find(text[0x634:0x64C], match + 1) < 0, 'driver not found in IOP RAM'
        self.base = base = match - 0x634
        relocated = {u32(rel, i) for i in range(0, len(rel), 8)}
        imports = {i for i in range(0x3130, len(text) - 4, 4)
                   if u32(text, i) == 0x03E00008 and u32(text, i + 4) & 0xFFFF0000 == 0x24000000}
        headers = {i for i in range(0x3130, len(text) - 8, 4) if u32(text, i) == 0x41E00000}
        metadata = {i + j for i in headers for j in (4, 8)}
        self.checked = 0
        for i in range(0, len(text), 4):
            if i in relocated or i in imports or i in metadata:
                continue
            assert text[i:i + 4] == iop[base + i:base + i + 4], ('driver word differs', hex(i))
            self.checked += 1
        self.iop = iop
        self.text_len = len(text)
        self.covered = set()

    def cpu(self, scripts):
        base, iop = self.base, self.iop
        cpu = Cpu(bytearray(iop), lambda pc: u32(iop, pc & 0x1FFFFF), ee=False, stack=IOP_STACK)
        cpu.log = []
        nax, envx, status = scripts

        def vi(reg): return ((reg & 1) * 24 + ((reg >> 1) & 0x1F)) % 48

        def hook(name):
            def run(c):
                a0, a1, a2, a3 = (c.r[i] & M32 for i in (4, 5, 6, 7))
                c.r[2] = 0
                if name == 'set_param':
                    c.log.append((5, a0 & 0xFFFF, a1 & 0xFFFF, 0, 0, 0))
                elif name == 'get_param':
                    c.log.append((6, a0 & 0xFFFF, 0, 0, 0, 0))
                    c.r[2] = envx[vi(a0 & 0xFFFF)]
                elif name == 'set_switch':
                    c.log.append((7, a0 & 0xFFFF, a1, 0, 0, 0))
                elif name == 'set_addr':
                    c.log.append((9, a0 & 0xFFFF, a1, 0, 0, 0))
                elif name == 'get_addr':
                    c.log.append((10, a0 & 0xFFFF, 0, 0, 0, 0))
                    c.r[2] = nax[vi(a0 & 0xFFFF)]
                elif name == 'voice_trans':
                    size = c.load(c.r[29] + 0x10, 4, track=False)
                    data = c.read(a2, size)
                    c.log.append((17, a0 & 0xFFFF, a1 & 0xFFFF, a3, size,
                                  fnv(data) if a2 == base + 0x46B0 else 0xDEADBEEF))
                elif name == 'trans_status':
                    c.log.append((19, a0 & 0xFFFF, a1 & 0xFFFF, 0, 0, 0))
                    c.r[2] = x64(status)
                elif name == 'memcpy':
                    c.write(a0, c.read(a1, sx(a2)), track=True)
                    c.r[2] = x64(a0)
                elif name == 'memset':
                    c.write(a0, bytes([a1 & 0xFF]) * sx(a2), track=True)
                    c.r[2] = x64(a0)
            return run
        for off, name in STUBS.items():
            cpu.hooks[base + off] = hook(name)
        return cpu

    def region_bytes(self, cpu):
        return {k: cpu.read(self.base + off, n) for k, (off, n) in REGIONS.items()}

    def allowed(self):
        out = set()
        for off, n in REGIONS.values():
            out.update(range(self.base + off, self.base + off + n))
        return out


def bss_image(iop, base, pokes=()):
    img = bytearray(iop[base:base + BSS_END])
    for off, data in pokes:
        img[off:off + len(data)] = data
    return img


class DriverCase:
    """One driver function call on both sides from the same state."""

    def __init__(self, oracle, native, img, scripts):
        self.o, self.n, self.img, self.scripts = oracle, native, img, scripts
        self.cpu = oracle.cpu(scripts)
        self.cpu.write(oracle.base, bytes(img))
        nax, envx, status = scripts
        native.lib.drv_script((C.c_uint32 * 48)(*nax), (C.c_uint16 * 48)(*envx), status)
        native.lib.drv_load(bytes(img))

    def call(self, name, args=(), words=None, label=''):
        o, n, cpu = self.o, self.n, self.cpu
        entry = o.base + DRIVER_FUNCS[name]
        cpu.log = []
        cpu.reads, cpu.writes = set(), set()
        if name == 'rpc':
            data = struct.pack(f'<{len(words)}I', *words)
            cpu.write(o.base + RPC_BUFFER, data)
            cpu.call(entry, (0x64, o.base + RPC_BUFFER, len(data)))
            native_args = (C.c_uint32 * max(1, len(words)))(*words)
            result = n.lib.drv_call(DRIVER_FUNCS[name], native_args, len(data))
        elif name == 'command':
            cmd_addr = o.base + RPC_BUFFER
            cpu.write(cmd_addr, struct.pack('<4I', *args))
            cpu.call(entry, (cmd_addr,))
            result = n.lib.drv_call(DRIVER_FUNCS[name], (C.c_uint32 * 4)(*args), 16)
        elif name == 'push':
            cpu.call(entry, args)
            result = n.lib.drv_call(DRIVER_FUNCS[name], (C.c_uint32 * 4)(*args), 16)
        else:
            cpu.call(entry)
            result = n.lib.drv_call(DRIVER_FUNCS[name], (C.c_uint32 * 4)(), 0)
        o.covered |= {pc - o.base for pc in cpu.covered}
        cpu.covered = set()
        assert result == 0 and n.lib.drv_fault() == 0, (label, name, result, n.lib.drv_fault())
        count = n.lib.drv_log(n.log)
        native_log = [tuple(n.log[6 * i:6 * i + 6]) for i in range(min(count, 16384))]
        assert native_log == cpu.log, dict(case=label, fn=name, native=native_log[:8], original=cpu.log[:8])
        bad = cpu.writes - o.allowed()
        assert not bad, (label, name, 'original store outside the modelled regions', sorted(hex(a) for a in bad)[:8])
        img = (C.c_uint8 * BSS_END)()
        n.lib.drv_save(img)
        native_bytes = bytes(img)
        for key, (off, size) in REGIONS.items():
            orig = cpu.read(o.base + off, size)
            if orig != native_bytes[off:off + size]:
                first = next(i for i in range(size) if orig[i] != native_bytes[off + i])
                raise AssertionError(dict(case=label, fn=name, region=key, offset=hex(off + first),
                                          original=orig[first:first + 16].hex(),
                                          native=native_bytes[off + first:off + first + 16].hex()))
        return cpu.log


def voice_rec(img, v):
    return list(struct.unpack_from('<13I', img, 0x76C0 + 0x34 * v))


def put_voice(img, v, fields):
    struct.pack_into('<13I', img, 0x76C0 + 0x34 * v, *[f & M32 for f in fields])


def random_state(img, rng, captured_voices):
    """A driver state near the captured one: voices 0..3 keep their captured
    configuration with random play bookkeeping; a few other voices get
    random stream records; the queue and pending word are randomised."""
    halves = (0, 0x1000000, 0x2000000, 0x3000000)
    for v in range(48):
        if v < 4:
            rec = list(captured_voices[v])
        elif rng.random() < 0.1:
            spu = rng.choice((0x15010, 0x19010, 0x1D010)) + rng.randrange(0, 4) * 0x4000
            rec = [0, rng.choice((0x10000, 0x20000)), spu, 0x4000, 0xADC00 + rng.randrange(0, 3) * 0x10100,
                   0x10000, 0x3FFF, 0x3FFF, 0xBB80, 0, 0, 0, 0]
        else:
            continue
        rec[0] = rng.choice((0, 1, 1, 3))
        rec[10] = rng.choice((0, 0x2000, 0x4000, 0x8000, 0xC000, 0xE000))
        rec[11] = rng.choice(halves)
        rec[12] = rng.choice(halves)
        put_voice(img, v, rec)
    return img


def nax_script(img, rng):
    out = []
    for v in range(48):
        rec = voice_rec(img, v)
        spu, half = rec[2], rec[3] // 2
        out.append(rng.choice((spu - 2, spu, spu + 2, spu + 0x100, spu + half - 2, spu + half, spu + half + 2,
                               spu + 2 * half - 2, spu + 2 * half + 0x10, rng.randrange(0, 0x20000))) & M32)
    return out


def queue_entry(rng, voice_img, v=None):
    v = rng.randrange(0, 4) if v is None else v
    rec = voice_rec(voice_img, v)
    kind = rng.choice((0x1000, 0x1000, 0x1000, 0x1010, 0x1011, 0x0000, 0x1234))
    if kind == 0x1000:
        half = rng.choice((0x1000000, 0x2000000, 0x0000000, 0x3000000))
        return [(v << 26) | half | (rec[1] & 0xFF0000) | 0x1000, rec[4] + rng.choice((0, 0x4000, 0x8000, 0xC000)),
                rec[2] + rng.choice((0, 0x2000)), rec[3] // 2]
    mask = rng.choice((1 << v, 0x3, 0xF, rng.getrandbits(24)))
    return [kind, mask, rng.choice((0, 0x1, rng.getrandbits(24))), 0]


def put_queue(img, rng, entries, start):
    struct.pack_into('<ii', img, 0x3494, start + len(entries), start)
    for i, e in enumerate(entries):
        struct.pack_into('<4I', img, 0x8080 + 16 * ((start + i) % 0x60), *[x & M32 for x in e])


def driver_cases(oracle, native, iop_img, rng, name):
    base = oracle.base
    img0 = bss_image(iop_img, base)
    captured = [voice_rec(img0, v) for v in range(4)]
    count = 0
    envx = [rng.choice((0, 0x7FFF, 0x1234, 0xFFFF)) for _ in range(48)]
    # -- commands
    cmds = []
    for v in (0, 1, 2, 3, 23, 24, 47):
        for flags in (0x20002, 0x10000, 0xFF0000):
            cmds.append([0x3E, (v << 24) | flags | 0x4000 | 0x01, (0x5010 << 16) | (0x10000 >> 8) & 0xFFFF,
                         ((0x10000 & 0xFF) << 24) | 0xAE000])
        cmds.append([0x3E, (v << 24) | rng.getrandbits(24), rng.getrandbits(32), rng.getrandbits(32)])
        cmds.append([0x3F, v, 0, 0])
    for kind in (0x40, 0x41, 0x42):
        for w1, w2 in ((1, 0), (3, 0), (0xF, 0), (0, 1), (0x800001, 0x800001), (0xFFFFFF, 0xFFFFFF), (0, 0)):
            cmds.append([kind, w1, w2, rng.choice((0x3FFF0000, 0x3FFF, 0xBB80, 0xAC44, 0x7FFFFFFF, 0x80000000,
                                                   0xFFFFFFFF, rng.getrandbits(32)))])
    for w1, w2 in ((1, 0), (0xF, 0), (0, 0x800000), (0xFFFFFF, 0xFFFFFF)):
        cmds.append([0x43, w1, w2, 0])
    for core in (0, 1, 0xFFFF0001):
        cmds.append([0x16, core, rng.getrandbits(32), rng.getrandbits(32)])
    cmds += [[0x3C, 0, 0, 0], [0x3D, 1, 2, 3]]
    chosen = select(cmds, 40, 0x634 ^ zlib.crc32(name.encode()) & 0xFFFF, axes=(lambda c: c[0],))
    TOTALS['commands'][0] += len(chosen)
    TOTALS['commands'][1] += len(cmds)
    for cmd in chosen:
        img = random_state(bytearray(img0), rng, captured)
        if cmd[0] == 0x42:   # a full queue drops entries
            put_queue(img, rng, [queue_entry(rng, img) for _ in range(rng.choice((0, 3, 0x5E)))],
                      rng.choice((0, 0x5F, 0x60 * 7 + 3)))
        case = DriverCase(oracle, native, img, (nax_script(img, rng), envx, 1))
        case.call('command', cmd, label=f'{name} cmd {cmd[0]:#x}')
        count += 1
    # -- rpc: sizes around the ring end
    for write in select((0, 0x10, 0xFE0, 0xFF0, 0x1000, 0x7FF0, 0x12340), 7, 0x12C):
        for n in (0, 1, 4, 255):
            img = bytearray(img0)
            struct.pack_into('<II', img, 0x34A0, write, write)
            case = DriverCase(oracle, native, img, ([0] * 48, envx, 1))
            case.call('rpc', words=[rng.getrandbits(32) for _ in range(4 * n)], label=f'{name} rpc {write:#x}/{n}')
            count += 1
    # -- tick: ring commands (stream subset) then the snapshot
    for trial in range(pick(12, 4)):
        img = random_state(bytearray(img0), rng, captured)
        ring_w = rng.choice((0, 0xFE0, 0x2340))
        words = []
        for _ in range(rng.randrange(0, 5)):
            words += rng.choice(cmds)
        struct.pack_into('<II', img, 0x34A0, ring_w + 4 * len(words), ring_w)
        data = struct.pack(f'<{len(words)}I', *[w & M32 for w in words])
        for i in range(0, len(data), 16):
            off = (ring_w + i) & 0xFFF
            img[0x66C0 + off:0x66C0 + off + 16] = data[i:i + 16]
        case = DriverCase(oracle, native, img, (nax_script(img, rng), envx, 1))
        case.call('tick', label=f'{name} tick {trial}')
        count += 1
    # -- scan
    for trial in range(pick(60, 12)):
        img = random_state(bytearray(img0), rng, captured)
        put_queue(img, rng, [], rng.choice((0, 0x5F, 0x60, 0x3F7)))
        case = DriverCase(oracle, native, img, (nax_script(img, rng), envx, 1))
        case.call('scan', label=f'{name} scan {trial}')
        count += 1
    # -- consume
    for trial in range(pick(120, 24)):
        img = random_state(bytearray(img0), rng, captured)
        entries = [queue_entry(rng, img) for _ in range(rng.choice((0, 1, 2)))]
        put_queue(img, rng, entries, rng.choice((0, 0x5F, 0x60 * 3 + 1)))
        pending = rng.choice((0, 0x10000000, 0x10000001, 0x10000003, 0xFFFFFFC1 & 0xFF | 0x10000000))
        if pending & 0xFF >= 48:
            pending = 0x10000002
        struct.pack_into('<I', img, 0x34A8, pending)
        case = DriverCase(oracle, native, img, (nax_script(img, rng), envx, rng.choice((1, 1, 0, 2))))
        case.call('consume', label=f'{name} consume {trial}')
        count += 1
    # -- push, around a full queue and the index wrap
    for start, n in ((0, 0), (0, 0x5F), (0, 0x60), (0x5F, 0x5F), (0x3F7, 0x60), (7, 1)):
        img = bytearray(img0)
        struct.pack_into('<ii', img, 0x3494, start + n, start)
        case = DriverCase(oracle, native, img, ([0] * 48, envx, 1))
        case.call('push', [rng.getrandbits(32) for _ in range(4)], label=f'{name} push {start}/{n}')
        count += 1
    return count


def driver_lockstep(oracle, native, iop_img, rng, name, ticks):
    """The driver loop, tick after tick, from the captured state: rpc (the
    lanes' commands: volume words every field, key-on/off, 0x16), then 0x328,
    0x20C8 and 0x23B8. NAX follows each active voice's buffer at a tick's
    worth of samples; transfers complete by the next tick."""
    img = bss_image(iop_img, oracle.base)
    envx = [0x7FFF] * 4 + [0] * 44
    pos = {v: rng.randrange(0, 0x4000) for v in range(48)}
    case = DriverCase(oracle, native, img, ([0] * 48, envx, 1))
    busy, steps = False, 0
    for t in range(ticks):
        words = []
        if t % 4 == 0:
            words += [0x40, 3, 0, rng.choice((0x3FFF, 0x3FFF0000, 0x1234))]
        if t in (ticks // 3, 2 * ticks // 3):
            words += [0x43, 0xC, 0, 0, 0x42, 0xC, 0, 0]
        if t == ticks // 2:
            words += [0x16, 0, 0, 0, 0x16, 1, 0x7FFF, 0x7FFF]
        nax = []
        for v in range(48):
            rec = voice_rec(case.cpu.read(oracle.base, BSS_END), v)
            if rec[0] & 1:
                pos[v] = (pos[v] + 111) % 0x4000
            nax.append((rec[2] + pos[v] + 2) & M32)
        status = 0 if busy else 1
        case.scripts = (nax, envx, status)
        native.lib.drv_script((C.c_uint32 * 48)(*nax), (C.c_uint16 * 48)(*envx), status)
        cpu = case.cpu
        o_hooks = oracle.cpu((nax, envx, status)).hooks
        cpu.hooks = o_hooks
        if t % 4 == 0:
            case.call('rpc', words=words, label=f'{name} lockstep {t} rpc')
        case.call('tick', label=f'{name} lockstep {t}')
        case.call('scan', label=f'{name} lockstep {t}')
        log = case.call('consume', label=f'{name} lockstep {t}')
        busy = any(e[0] == 17 for e in log)
        steps += 4
    return steps


# ----------------------------------------------------------------- EE oracle

class EeOracle:
    def __init__(self, elf, capture):
        self.elf, self.capture = elf, capture

    def cpu(self):
        elf = self.elf
        return Cpu(self.capture, lambda pc: u32(elf, pc - 0x100000 + 0x300), ee=True, stack=EE_STACK)


def ee_cases(elf, captures, native, rng):
    capture = captures['route/' + ROUTES[-1]]
    for name, ee in captures.items():
        for start, size in ((0x11A2B0, 0x1C0), (0x1FA6A0, 0x34), (0x1AB1E0, 0x190), (0x1FA6E0, 0xA8)):
            assert ee[start:start + size] == elf[start - 0x100000 + 0x300:start - 0x100000 + 0x300 + size], \
                (name, hex(start), 'capture code differs from the ELF')
    o = EeOracle(elf, capture)
    lib = native.lib
    lib.ee_open()
    count = 0
    # 0011A2B0
    table0 = capture[0x27CCC0:0x27CCC0 + 0x30 * 0x6A]
    tables = [bytes(table0), bytes(0x30 * 0x6A)]
    for _ in range(pick(60, 14)):
        t = bytearray(0x30 * 0x6A)
        for i in range(0x30):
            e = i * 0x6A
            struct.pack_into('<H', t, e, rng.choice((0, 0, 1, 2)))
            struct.pack_into('<H', t, e + 0x1A, rng.choice((0, 1, 2, 3, 3)))
            struct.pack_into('<H', t, e + 0x0A, rng.choice((0, 5, 0xFFFF, rng.getrandbits(16))))
            t[e + 0x20:e + 0x30] = rng.randbytes(16)
        tables.append(bytes(t))
    busy = bytearray(tables[-1])
    for i in range(0x30):
        struct.pack_into('<H', busy, i * 0x6A, 1)
    tables.append(bytes(busy))
    for t in tables:
        for a0 in (0, 1, 2, 3, -1):
            mask = rng.getrandbits(64)
            cpu = o.cpu()
            cpu.write(0x27CCC0, t)
            cpu.write(0x27F740 + 0x28, struct.pack('<Q', mask))
            calls = []

            def iop(c, calls=calls):
                calls.append(tuple(sx(c.r[i]) for i in (4, 5, 6, 7)))
                c.r[2] = 1

            def memset(c):
                c.write(c.r[4] & M32, bytes([c.r[5] & 0xFF]) * sx(c.r[6]), track=True)
                calls.append(('memset', c.r[4] & M32, c.r[5] & 0xFF, sx(c.r[6])))
            cpu.hooks = {0x1157F0: iop, 0x121A28: memset}
            voice = sx(cpu.call(0x11A2B0, (a0,)))
            nt = (C.c_uint8 * len(t))(*t)
            nm = C.c_uint64(mask)
            nv, nq, nc = C.c_int32(), (C.c_uint32 * 4)(), C.c_uint32()
            lib.ee_reset()
            assert lib.ee_0011A2B0(nt, C.byref(nm), a0, C.byref(nv), nq, C.byref(nc)) == 0
            assert nv.value == voice, ('0011A2B0 result', a0, nv.value, voice)
            assert bytes(nt) == cpu.read(0x27CCC0, len(t)), ('0011A2B0 table', a0)
            assert nm.value == struct.unpack('<Q', cpu.read(0x27F768, 8))[0], ('0011A2B0 mask', a0)
            iop_calls = [c for c in calls if c[0] != 'memset']
            native_calls = [tuple(sx(x) for x in nq)] if nc.value else []
            assert native_calls == iop_calls, ('0011A2B0 001157F0', a0, native_calls, iop_calls)
            assert all(c[1:] == (0x27CCC0 + voice * 0x6A, 0, 0x6A) for c in calls if c[0] == 'memset')
            allowed = set(range(0x27CCC0, 0x27CCC0 + len(t))) | set(range(0x27F768, 0x27F770))
            assert cpu.writes <= allowed and cpu.reads <= allowed, ('0011A2B0 touched', a0)
            count += 1
    # 001FA6A0 over scripted 0010F8F8 results
    for value in (0x85B00, 0x85B04, 0x85B0F, 0x85B10, 0, 0xFFFFFFF1, 0x1FFFF8):
        for size in (0x10000, 0x28000, 1, 0x7FFFFFE0):
            cpu = o.cpu()
            args = []

            def alloc(c, args=args, value=value):
                args.append(sx(c.r[4]))
                c.r[2] = x64(value)
            cpu.hooks = {0x10F8F8: alloc}
            want = cpu.call(0x1FA6A0, (size,)) & M32
            after = C.c_uint32()
            got = lib.ee_001FA6A0(value, size, C.byref(after))
            if size + 0x10 <= 0x200000 - value:   # the native heap model faults otherwise
                assert got == want, ('001FA6A0', hex(value), hex(size), hex(got), hex(want))
                assert args == [size + 0x10], args
                assert after.value == value + ((size + 0x10 + 0xFF) & ~0xFF), hex(after.value)
                count += 1
            lib.ee_reset()
    # sub_cdrom0_IRX_SNDN2DRV_IRX_1: every worker scripted, 0010F8F8 from the native heap model
    cpu = o.cpu()
    heap = [0x85B00]
    sizes = []

    def alloc(c):
        size = sx(c.r[4])
        sizes.append(size)
        c.r[2] = x64(heap[0])
        heap[0] += (size + 0xFF) & ~0xFF

    def ok(c): c.r[2] = 1

    def zero(c): c.r[2] = 0
    cpu.hooks = {0x10F8F8: alloc, 0x10E088: zero, 0x112F98: zero, 0x1138D8: zero, 0x110508: ok, 0x1104C0: ok,
                 0x10F140: zero, 0x10F870: zero, 0x110028: zero}
    cpu.call(0x1AB1E0)
    stored = [u32(cpu.read(a, 4), 0) for a in (0x275B50, 0x275B28, 0x275B4C, 0x275B24, 0x275B20)]
    lib.ee_reset()
    out = (C.c_uint32 * 5)()
    assert lib.ee_boot(out) == 0 and list(out) == stored, ('boot buffers', [hex(x) for x in out], [hex(x) for x in stored])
    assert sizes == [0x28010, 0x10010, 0x10010, 0x10010], sizes
    count += 1
    # sub_O_STREAM_MUSIC_DAT_1 with a scripted directory search (fails twice first)
    for fails in (0, 2):
        cpu = o.cpu()
        seq, names = [], []
        state = {'left': fails}

        def init(c): seq.append(('00113280', sx(c.r[4]))); c.r[2] = 1

        def search(c):
            name = c.cstr(c.r[5] & M32)
            names.append(name)
            seq.append(('00111C28', name))
            if state['left']:
                state['left'] -= 1
                c.r[2] = 0
                return
            c.save(c.r[4] & M32, 0x9D911 if b'MUSIC' in name else 0xC3257, 4, track=False)
            c.r[2] = 1
        cpu.hooks = {0x113280: init, 0x111C28: search}
        cpu.call(0x1FA6E0)
        got = [u32(cpu.read(0x282188, 4), 0), u32(cpu.read(0x28218C, 4), 0)]
        assert seq[0] == ('00113280', 0) and seq.count(('00113280', 0)) == 1 + fails, seq
        search_names = [n for n in names]
        m, v = search_names[0].decode(), search_names[-1].decode()
        out2 = (C.c_uint32 * 2)()
        assert lib.ee_sub_O(m.encode(), v.encode(), m.encode(), v.encode(), 0x9D911, 0xC3257, out2) == 0
        assert list(out2) == got == [0x9D911, 0xC3257], (list(out2), got)
        count += 1
    return count


# ----------------------------------------------------------------- captures

def lane_record(ee, lane):
    r = 0x281FD0 + 0x60 * lane
    return dict(state=ee[r], half=ee[r + 1], last_half=ee[r + 2], load=ee[r + 3], voice=s32(ee, r + 4),
                buffer=u32(ee, r + 0x14), size=u32(ee, r + 0x18), loop=s32(ee, r + 0x20),
                loop_sector=u32(ee, r + 0x24), end=u32(ee, r + 0x28), base=u32(ee, r + 0x2C),
                read_sector=u32(ee, r + 0x30), read_count=u32(ee, r + 0x34), read_addr=u32(ee, r + 0x38),
                duration=u32(ee, r + 0x4C), start=u32(ee, r + 0x50), step=u32(ee, r + 0x54),
                volume=u32(ee, r + 0x58), active=sx(ee[0x282154 + lane], 8), cue=s32(ee, 0x282178 + 4 * lane))


class Disc:
    def __init__(self, path):
        blob = path.read_bytes()
        assert blob[:4] == b'EMST' and u32(blob, 4) == 1
        self.lsn = (u32(blob, 8), u32(blob, 12))
        rows_at = 0xA8
        self.rows = [struct.unpack_from('<4i', blob, rows_at + 16 * i) for i in range(68 + 179)]
        n = u32(blob, 0x20)
        ext_at = rows_at + 16 * (68 + 179)
        self.extents = [struct.unpack_from('<4I', blob, ext_at + 16 * i) for i in range(n)]
        self.blob = blob

    def sectors(self, lsn, count):
        out = b''
        for i in range(count):
            for e_lsn, e_n, off, _file in self.extents:
                if e_lsn <= lsn + i < e_lsn + e_n:
                    out += self.blob[off + (lsn + i - e_lsn) * 2048:off + (lsn + i - e_lsn + 1) * 2048]
                    break
            else:
                return None
        return out


def spu_ram_offset(spu2, iop, base):
    """Where SPU RAM starts inside SPU2.bin: the driver's staging buffer (its
    last transfer, flags included) must sit at one stream half."""
    staging = iop[base + 0x46B0:base + 0x46B0 + 0x2000]
    pos = spu2.find(staging)
    assert pos >= 0 and spu2.find(staging, pos + 1) < 0, 'last transfer not found in SPU2 RAM'
    candidates = [pos - a for a in (0x5010, 0x7010, 0x9010, 0xB010, 0xD010, 0xF010, 0x11010, 0x13010)]
    return candidates


def capture_evidence(captures, routes, states, disc, irx, native):
    checks = 0
    report = {}
    # boot buffers and the stream start sectors in all images
    native.lib.ee_reset()
    out = (C.c_uint32 * 5)()
    native.lib.ee_boot(out)
    for name, ee in captures.items():
        stored = [u32(ee, a) for a in (0x275B50, 0x275B28, 0x275B4C, 0x275B24, 0x275B20)]
        assert stored == list(out), (name, [hex(x) for x in stored])
        assert (u32(ee, 0x282188), u32(ee, 0x28218C)) == disc.lsn, name
        checks += 6
    # the SPU RAM offset inside SPU2.bin must be one and the same in every state
    offsets = None
    for n in routes:
        iop, spu2 = states[n]
        base = DriverOracle(irx, iop).base
        cand = set(spu_ram_offset(spu2, iop, base))
        # the first block of each stream voice's first half carries flag 6,
        # of its second half flag 2 (the staging model; checked in full below)
        cand = {c for c in cand if c >= 0 and all(spu2[c + a + 1] == f for a, f in
                                                  ((0x5010, 6), (0x7010, 2), (0x9010, 6), (0xB010, 2)))}
        offsets = cand if offsets is None else offsets & cand
    assert offsets and len(offsets) == 1, offsets
    spu_off = offsets.pop()
    report['spu2_ram_offset'] = hex(spu_off)
    # the native driver's configuration after the native 001F9820 (sim boot)
    lib = native.lib
    assert lib.sim_open(str(EMST).encode(), 0, 1) == 0
    for _ in range(3):
        assert lib.sim_field(0) == 0
    native_voices = []
    for v in range(4):
        o = (C.c_uint32 * 13)()
        lib.sim_voice(v, o)
        native_voices.append(list(o))
    for n in routes:
        iop, spu2 = states[n]
        base = DriverOracle(irx, iop).base
        ee = captures['route/' + n]
        for v in range(4):
            cap = list(struct.unpack_from('<13I', iop, base + 0x76C0 + 0x34 * v))
            for k in (1, 2, 3, 4, 5, 8):    # stride, SPU, size, IOP, IOP size, rate
                assert cap[k] == native_voices[v][k], (n, v, k, hex(cap[k]), hex(native_voices[v][k]))
                checks += 1
            # idle voice lanes keep the boot volume words; the lane-0 pair is (0, v) / (v, 0)
            if v >= 2:
                assert cap[6:8] == native_voices[v][6:8], (n, v, cap[6:8])
            else:
                assert (cap[6] == 0) == (v == 0) and (cap[7] == 0) == (v == 1), (n, v, cap[6:8])
            checks += 1
            # the IOP snapshot and the EE copy: the EE word is the snapshot or the one before it
            snap = u32(iop, base + 0x4570 + 4 * v)
            assert u32(ee, 0x281880 + 4 * v) in (snap, (snap - (0x4000 if v < 2 else 0x2000)) % 0x10000), (n, v)
            checks += 1
        # lane-0 IOP buffer halves hold the sectors the lane's read state names
        lane = lane_record(ee, 0)
        if lane['active']:
            span = lane['end'] - lane['loop_sector']

            def back(sector, k):
                return lane['loop_sector'] + (sector - lane['loop_sector'] - k) % span
            if lane['state'] == 0:
                newest = 2 if lane['last_half'] == 2 else 1   # half 2 means the first half was refilled
                first_half_start = back(lane['read_sector'], 16) if newest == 2 else back(lane['read_sector'], 32)
                second_half_start = back(lane['read_sector'], 32) if newest == 2 else back(lane['read_sector'], 16)
                for which, start in ((0, first_half_start), (1, second_half_start)):
                    want = disc.sectors(start, 16)
                    got = iop[lane['buffer'] + 0x8000 * which:lane['buffer'] + 0x8000 * (which + 1)]
                    assert want is not None and got == want, (n, 'IOP half', which, start)
                    checks += 1
            # the stream voices' SPU halves: de-interleaved exported sectors with the model's flags
            spu = spu2[spu_off:spu_off + 0x200000]
            cue_data = disc.sectors(lane['loop_sector'], lane['end'] - lane['loop_sector'])
            channels = [b''.join(cue_data[i + 0x400 * c:i + 0x400 * (c + 1)] for i in range(0, len(cue_data), 0x800))
                        for c in (0, 1)]
            for v, channel in ((0, 1), (1, 0)):
                for h, flags in ((0, (6, 2)), (1, (2, 3))):
                    got = spu[0x5010 + 0x4000 * v + 0x2000 * h:][:0x2000]
                    pos = channels[channel].find(got[0x20:0x60])
                    assert pos >= 0x20, (n, 'SPU half not in the cue', v, h)
                    want = bytearray(channels[channel][pos - 0x20:pos - 0x20 + 0x2000])
                    assert (pos - 0x20) % 0x400 == 0, (n, 'SPU half offset', v, h, hex(pos))
                    want[1], want[0x2000 - 0x10 + 1] = flags
                    assert bytes(want) == got, (n, 'SPU half', v, h)
                    checks += 1
    report['capture_checks'] = checks
    return checks, report


# ----------------------------------------------------------------- co-simulation

GLOBALS = ((0x810E90, 4), (0x8106C8, 4), (0x810D38, 4), (0x810700, 1), (0x8104E4, 1), (0x8106F4, 1), (0x8106F5, 1))


class Sim:
    def __init__(self, native, latency=0, seed=1):
        self.lib = native.lib
        assert self.lib.sim_open(str(EMST).encode(), latency, seed) == 0
        self.g = (C.c_int32 * 7)()
        self.lib.sim_globals(0, self.g)

    def set(self, **kw):
        names = ('counter', 'd8106C8', 'd810D38', 'd810700', 'd8104E4', 'hold0', 'hold1')
        self.lib.sim_globals(0, self.g)
        for k, v in kw.items():
            self.g[names.index(k)] = v
        self.lib.sim_globals(1, self.g)

    def get(self, key):
        self.lib.sim_globals(0, self.g)
        return self.g[('counter', 'd8106C8', 'd810D38', 'd810700', 'd8104E4', 'hold0', 'hold1').index(key)]

    def field(self, service=True):
        r = self.lib.sim_field(int(service))
        if r:
            a = C.c_uint32()
            raise AssertionError(('sim fault', r, hex(self.lib.sim_fault(C.byref(a))), hex(a.value)))

    def call(self, fn, *args):
        a = list(args) + [0] * (4 - len(args))
        assert self.lib.sim_call(fn, *a) == 0, hex(fn)

    def lane(self, i):
        o = (C.c_int32 * 27)()
        self.lib.sim_lane(i, o)
        keys = ('state', 'half', 'last_half', 'load', 'voice', 'buffer', 'size', 'loop', 'loop_sector', 'end',
                'base', 'read_sector', 'read_count', 'read_addr', 'remaining', 'duration', 'start', 'step',
                'volume', 'release', 'active', 'cue', 'read_phase', 'read_lane', 'voice_right')
        d = dict(zip(keys, o))
        for k in ('buffer', 'size', 'loop_sector', 'end', 'base', 'read_sector', 'read_count', 'read_addr',
                  'duration', 'start', 'step', 'volume'):
            d[k] &= M32
        return d

    def status(self):
        ee, drv = (C.c_int32 * 128)(), (C.c_uint32 * 128)()
        self.lib.sim_status(ee, drv)
        return [x & M32 for x in ee], list(drv)

    def voice(self, v):
        o = (C.c_uint32 * 13)()
        self.lib.sim_voice(v, o)
        return list(o)


def settled(lane):
    """The half and read sector a lane holds once its read in flight lands
    (a lane in state 1 is mid-read: its sector advances by the count)."""
    rs = lane['read_sector'] + (lane['read_count'] if lane['state'] == 1 else 0)
    if rs >= lane['end']:
        rs = lane['loop_sector'] + rs - lane['end']
    return (lane['half'] if lane['state'] == 1 else lane['last_half'], rs - lane['base'])


def compare_lane(sim_lane, cap, keys):
    return {k: (sim_lane[k], cap[k]) for k in keys if sim_lane[k] != cap[k]}


def cue_of(disc, lane, cap, voice_lsn, music_lsn):
    if lane == 0:
        return cap['cue']
    for c in range(1, 179):
        if voice_lsn + disc.rows[68 + c][0] == cap['base']:
            return c
    raise AssertionError('voice cue not found')


def play_until(sim, lane, cue, elapsed, cadence, hold=None, samples=None):
    """Start the cue on an idle lane (fade-in, as 001FAE70 does), run until the
    key-on, then `elapsed` more fields. Returns the per-field history after
    key-on: (lane record, ee status, driver status, voice records)."""
    if hold is not None:
        sim.set(**{('hold0' if lane == 0 else 'hold1'): hold})
    sim.call(0x1FABF0, lane, cue, 270, 1)
    fields = 0
    while sim.lane(lane)['active'] != 2:
        sim.field(fields % cadence == 0)
        fields += 1
        key = 'hold0' if lane == 0 else 'hold1'
        if hold is not None and sim.get(key) == 1:   # the message service's release of the hold
            sim.set(**{key: 0})
        assert fields < 400, 'no key-on'
    start = sim.lane(lane)['start']
    history = {}
    while True:
        counter = sim.get('counter')
        if counter - start >= 0:
            history[counter - start] = (sim.lane(lane), *sim.status())
        if counter - start >= elapsed:
            break
        sim.field(fields % cadence == 0)
        fields += 1
    return history


def cosim(captures, routes, states, disc, irx, native, rng):
    lib = native.lib
    results = {'cursor': [0, 0], 'lane': [0, 0], 'settled': [0, 0], 'iop_voice': [0, 0], 'voice_lanes': [0, 0]}
    lines = []
    keys_settled = ('state', 'half', 'last_half', 'load')
    music_lsn, voice_lsn = disc.lsn
    # --- every active lane-0 image: start its cue, compare at its elapsed field count
    groups = {}
    for name, ee in captures.items():
        cap = lane_record(ee, 0)
        if cap['active'] != 2:
            continue
        elapsed = (u32(ee, 0x810E90) - cap['start']) & M32
        groups.setdefault(cap['cue'], []).append((elapsed, name, cap, ee))
    for cadence in (1, 2):
        for cue, items in sorted(groups.items()):
            sim = Sim(native)
            history = play_until(sim, 0, cue, max(e for e, *_ in items), cadence)
            for elapsed, name, cap, ee in items:
                lane, ee_status, drv_status = history[elapsed]
                word = u32(ee, 0x281880)
                ok = ee_status[48] == word and ee_status[49] == u32(ee, 0x281884)
                results['cursor'][0] += ok
                results['cursor'][1] += 1
                rel_sim = (lane['read_sector'] - lane['base'], lane['read_addr'] - lane['buffer'])
                rel_cap = (cap['read_sector'] - cap['base'], cap['read_addr'] - cap['buffer'])
                diff = compare_lane(lane, cap, keys_settled)
                lane_ok = not diff and rel_sim == rel_cap
                settled_ok = settled(lane) == settled(cap)
                results['settled'][0] += settled_ok
                results['settled'][1] += 1
                if cadence == 1:
                    assert ok, (name, 'cursor word', hex(word), hex(ee_status[48]))
                    assert lane_ok or (cap['state'] == 1 and settled_ok), (name, 'lane', diff, rel_sim, rel_cap)
                results['lane'][0] += lane_ok
                results['lane'][1] += 1
                if cadence == 1 or not (ok and lane_ok):
                    lines.append(f'  [cadence {cadence}]' if cadence != 1 else '')
                    lines.append(f"  {name:32s} cue {cue:2d} +{elapsed:4d}: cursor {word:#7x} sim {ee_status[48]:#7x}"
                                 f" {'=' if ok else 'X'}  lane {cap['state']}/{cap['half']}/{cap['last_half']}/"
                                 f"{cap['load']} rs+{rel_cap[0]} ra+{rel_cap[1]:#x} sim {lane['state']}/{lane['half']}/"
                                 f"{lane['last_half']}/{lane['load']} rs+{rel_sim[0]} ra+{rel_sim[1]:#x}"
                                 f" {'=' if lane_ok else 'X'}")
                if name.startswith('route/') and cadence == 1:
                    n = name.split('/', 1)[1]
                    iop = states[n][0]
                    base = DriverOracle(irx, iop).base
                    for v in (0, 1):
                        capv = struct.unpack_from('<13I', iop, base + 0x76C0 + 0x34 * v)
                        simv = sim.voice(v) if elapsed == max(e for e, *_ in items) else None
                        snap = u32(iop, base + 0x4570 + 4 * v)
                        good = drv_status[48 + v] == snap
                        results['iop_voice'][0] += good
                        results['iop_voice'][1] += 1
                        if not good:
                            lines.append(f'    {name} IOP snapshot voice {v}: sim {drv_status[48 + v]:#x} capture {snap:#x}')
                        del simv, capv
    # --- voice lanes (routes 10..14): play the clip to its timer end, compare the final record
    seen = set()
    for name, ee in captures.items():
        for lane_i in (1, 2):
            cap = lane_record(ee, lane_i)
            if cap['base'] == 0 or (cap['base'], lane_i) in seen:
                continue
            seen.add((cap['base'], lane_i))
            cue = cue_of(disc, lane_i, cap, voice_lsn, music_lsn)
            sim = Sim(native)
            play_until(sim, lane_i, cue, 0, 1)
            fields = 0
            while sim.lane(lane_i)['active']:
                sim.field(True)
                fields += 1
                assert fields < 20000
            lane = sim.lane(lane_i)
            keys = ('state', 'half', 'last_half', 'load', 'read_sector', 'read_count', 'read_addr', 'base', 'end',
                    'loop_sector', 'duration')
            diff = compare_lane(lane, cap, keys)
            results['voice_lanes'][0] += not diff
            results['voice_lanes'][1] += 1
            assert not diff, (name, 'voice lane', lane_i, diff)
            lines.append(f"  {name:32s} voice lane {lane_i} cue {cue}: {'=' if not diff else diff}")
    return results, lines


def scenario_opening(captures, native):
    """The opening: 001FD4C0(0x66) starts lane 0 on cue 63 with the hold byte
    D_008106F4 = 2; the prefill parks the lane in state 2 (hold 1) until the
    message service releases it; RESUME_MUSIC 001FAE70(0) later swaps to cue 25.
    Compared: opening_ee (cue 63 at its elapsed count), handoff_ee (the field
    before cue 25's key-on) and playable_ee (cue 25 at its elapsed count)."""
    op = lane_record(captures['ref/opening_ee.bin'], 0)
    ho_ee = captures['ref/handoff_ee.bin']
    pl = lane_record(captures['ref/playable_ee.bin'], 0)
    elapsed_opening = u32(captures['ref/opening_ee.bin'], 0x810E90) - op['start']
    swap_at = u32(ho_ee, 0x810E90) - op['start']            # the handoff image's own counter
    sim = Sim(native)
    history = play_until(sim, 0, 63, swap_at - 1, 1, hold=2)
    lane, ee_status, _ = history[elapsed_opening]
    out = []
    out.append(('opening cursor', ee_status[48], u32(captures['ref/opening_ee.bin'], 0x281880)))
    out.append(('opening lane', (lane['state'], lane['half'], lane['last_half'], lane['read_sector'] - lane['base']),
                (op['state'], op['half'], op['last_half'], op['read_sector'] - op['base'])))
    # the swap: 001FAE70(0) with the AREA11 globals of the handoff image
    g = {k: v for k, v in zip(('d8106C8', 'd810D38', 'd810700', 'd8104E4'),
                             (s32(ho_ee, 0x8106C8), s32(ho_ee, 0x810D38), ho_ee[0x810700], ho_ee[0x8104E4]))}
    sim.set(**g)
    sim.call(0x1FAE70, 0)
    fields = 0
    handoff_seen = None
    while True:
        sim.field(True)
        fields += 1
        lane = sim.lane(0)
        if lane['cue'] == 25 and lane['state'] == 1 and lane['load'] == 2 and lane['active'] == 1:
            handoff_seen = (lane, sim.status()[0])
        if lane['cue'] == 25 and lane['active'] == 2:
            break
        assert fields < 400
    ho = lane_record(ho_ee, 0)
    if handoff_seen:
        hl, hs = handoff_seen
        out.append(('handoff lane', (hl['state'], hl['half'], hl['last_half'], hl['load'], hl['read_sector'] - hl['base']),
                    (ho['state'], ho['half'], ho['last_half'], ho['load'], ho['read_sector'] - ho['base'])))
        out.append(('handoff stale cursor nonzero', hs[48] != 0, u32(ho_ee, 0x281880) != 0))
    else:
        out.append(('handoff lane', 'not observed (the read completed within the key-on service)', 'state 1 load 2'))
    start = sim.lane(0)['start']
    target = u32(captures['ref/playable_ee.bin'], 0x810E90) - pl['start']
    while sim.get('counter') - start < target:
        sim.field(True)
    lane = sim.lane(0)
    ee_status = sim.status()[0]
    out.append(('playable cursor', ee_status[48], u32(captures['ref/playable_ee.bin'], 0x281880)))
    out.append(('playable lane', (lane['state'], lane['half'], lane['last_half'], lane['read_sector'] - lane['base']),
                (pl['state'], pl['half'], pl['last_half'], pl['read_sector'] - pl['base'])))
    return out


def scenario_status(captures, native):
    """The status page: open = STOP_STREAMS 001FABB0, close = RESUME_MUSIC
    001FAE70(1). The idle images (status hub, panel, panel root) hold every
    lane idle and every status word 0; the images 12 fields after a resume
    (panel animation, route 01) hold the resumed cue's state."""
    out = []
    sim = Sim(native)
    play_until(sim, 0, 25, 500, 1)
    sim.call(0x1FABB0)
    for _ in range(4):
        sim.field(True)
    ee_status = sim.status()[0]
    for name in ('ref/status-hub/eeMemory.bin', 'ref/panel/eeMemory.bin', 'ref/panel/root/eeMemory.bin'):
        ee = captures[name]
        cap_status = [u32(ee, 0x2817C0 + 4 * i) for i in range(96)]
        lanes = [lane_record(ee, i)['active'] for i in range(3)]
        sim_lanes = [sim.lane(i)['active'] for i in range(3)]
        out.append((f'{name} idle', (ee_status[48:52], ee_status[0:4] == [0] * 4, sim_lanes),
                    (cap_status[48:52], cap_status[0:4] == [0] * 4, lanes)))
    for name in ('ref/panel/animation_ee.bin', 'route/01_battery'):
        ee = captures[name]
        cap = lane_record(ee, 0)
        elapsed = u32(ee, 0x810E90) - cap['start']
        sim2 = Sim(native)
        play_until(sim2, 0, 25, 300, 1)
        sim2.call(0x1FABB0)
        for _ in range(8):
            sim2.field(True)
        sim2.set(d8106C8=s32(ee, 0x8106C8), d810D38=s32(ee, 0x810D38), d810700=ee[0x810700], d8104E4=ee[0x8104E4])
        sim2.call(0x1FAE70, 1)
        fields = 0
        while sim2.lane(0)['active'] != 2:
            sim2.field(True)
            fields += 1
        start = sim2.lane(0)['start']
        while sim2.get('counter') - start < elapsed:
            sim2.field(True)
        lane = sim2.lane(0)
        st = sim2.status()[0]
        out.append((f'{name} resume +{elapsed}',
                    (st[48], lane['state'], lane['half'], lane['last_half'], lane['read_sector'] - lane['base'], lane['cue']),
                    (u32(ee, 0x281880), cap['state'], cap['half'], cap['last_half'], cap['read_sector'] - cap['base'],
                     cap['cue'])))
    return out


def continuity(native, disc):
    """A long lane-0 run of cue 25: voice 1 (left) and voice 0 (right) must
    play the exported cue's channels in order, over buffer wraps and the loop."""
    lib = native.lib
    sim = Sim(native)
    frames = pick(3_000_000, 700_000)
    left, right = (C.c_int16 * frames)(), (C.c_int16 * frames)()
    lib.sim_tap(left, right, frames, 1, 0)
    play_until(sim, 0, 25, frames // 800 + 4, 1)
    n = (C.c_uint32 * 2)()
    lib.sim_tap_count(n)
    row = disc.rows[25]
    start = disc.lsn[0] + row[0]
    count = (row[2] + 2047) // 2048
    data = disc.sectors(start, count)
    lch = b''.join(data[i:i + 0x400] for i in range(0, len(data), 0x800))
    rch = b''.join(data[i + 0x400:i + 0x800] for i in range(0, len(data), 0x800))

    def decode(ch, total):
        coef = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))
        out, s1, s2, blocks = [], 0, 0, len(ch) // 16
        b = 0
        while len(out) < total:
            p = (b % blocks) * 16
            shift, filt = ch[p] & 15, ch[p] >> 4
            filt = 0 if filt > 4 else filt
            shift = 12 if shift > 12 else shift
            for i in range(28):
                nib = (ch[p + 2 + (i >> 1)] >> ((i & 1) * 4)) & 15
                nib = nib - 16 if nib > 7 else nib
                x = (nib << (12 - shift)) + ((s1 * coef[filt][0] + s2 * coef[filt][1]) >> 6)
                x = max(-32768, min(32767, x))
                s2, s1 = s1, x
                out.append(x)
            b += 1
        return out[:total]
    got_l, got_r = list(left[:n[0]]), list(right[:n[1]])
    want_l, want_r = decode(lch, len(got_l)), decode(rch, len(got_r))
    first = next((i for i in range(len(got_l)) if got_l[i] != want_l[i]), None)
    first_r = next((i for i in range(len(got_r)) if got_r[i] != want_r[i]), None)
    assert first is None and first_r is None, ('stream continuity', first, first_r)
    return len(got_l), len(lch) // 16 * 28


# ----------------------------------------------------------------- main

ROUTES = []
TOTALS = {'commands': [0, 0]}


def main():
    started = time.time()
    global ROUTES
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA, 'boot ELF is not the pinned SCUS-97112'
    if not EMST.exists():
        sys.exit('assets/streams/streams.emst is missing: run python3 tools/export_streams.py')
    iso = DECOMP / 'Extermination-rebuilt.iso'
    _lsn, irx = iso_file(iso, '/IRX/SNDN2DRV.IRX')
    captures, ROUTES = load_captures()
    assert len(captures) >= 26, f'expected at least the 26 images, found {len(captures)}'
    states = extract_states(ROUTES)
    disc = Disc(EMST)
    work = Path(tempfile.mkdtemp(prefix='run_', dir=ROOT / 'build/iop_stream_reference'))
    try:
        native = Native(work)
        rng = random.Random(0x1A2B)
        driver_count, lock_steps, checked = 0, 0, 0
        oracles = []
        for n in select(ROUTES, 3, 0x51D, keep=lambda i, x: x in ('04_elevator_ride', '14_roger_encounter')):
            iop = states[n][0]
            oracle = DriverOracle(irx, iop)
            checked = oracle.checked
            native.lib.drv_open(iop)
            driver_count += driver_cases(oracle, native, iop, rng, n)
            lock_steps += driver_lockstep(oracle, native, iop, rng, n, pick(160, 40))
            oracles.append(oracle)
        covered = set().union(*(o.covered for o in oracles))
        missed = []
        for lo, hi in DRIVER_RANGES:
            for pc in range(lo, hi, 4):
                if pc not in covered:
                    missed.append(pc)
        unreached = set(missed) - UNREACHED
        if FULL or len(oracles) >= 2:
            assert not unreached, ('driver words not executed', [hex(x) for x in sorted(unreached)])
        ee_count = ee_cases(elf, captures, native, rng)
        cap_checks, report = capture_evidence(captures, ROUTES, states, disc, irx, native)
        results, lines = cosim(captures, ROUTES, states, disc, irx, native, rng)
        opening = scenario_opening(captures, native)
        status = scenario_status(captures, native)
        played, cue_samples = continuity(native, disc)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print('driver module: %d non-relocated words equal to the disc module; driver cases %d, lockstep calls %d'
          % (checked, driver_count, lock_steps))
    print('driver coverage: %d of %d translated words executed; not reached: %s'
          % (sum(1 for lo, hi in DRIVER_RANGES for pc in range(lo, hi, 4)) - len(missed),
             sum(1 for lo, hi in DRIVER_RANGES for pc in range(lo, hi, 4)),
             ' '.join(hex(pc) for pc in missed[:80]) + (' ...' if len(missed) > 80 else '')))
    print(f'EE oracle cases: {ee_count}')
    print(f"capture checks: {cap_checks} (SPU2 RAM at {report['spu2_ram_offset']} in SPU2.bin)")
    print('co-simulation against the captures (cadence 1 and 2 fields per lane service):')
    for k, (a, b) in results.items():
        print(f'  {k}: {a}/{b}')
    for line in lines:
        print(line)
    bad = [x for x in opening + status if x[1] != x[2]]
    assert not bad, bad
    print('opening scenario:')
    for label, sim_v, cap_v in opening:
        print(f"  {label}: sim {sim_v} capture {cap_v} {'=' if sim_v == cap_v else 'X'}")
    print('status page scenario:')
    for label, sim_v, cap_v in status:
        print(f"  {label}: sim {sim_v} capture {cap_v} {'=' if sim_v == cap_v else 'X'}")
    print(f'continuity: {played} samples per channel of cue 25 ({cue_samples} per loop) equal to the independent decode')
    banner(part(TOTALS['commands'][0], TOTALS['commands'][1], 'command cases'),
           f'{driver_count} driver cases over {len(oracles)} of {len(ROUTES)} IOP images', f'{time.time() - started:.1f} s')


if __name__ == '__main__':
    main()
