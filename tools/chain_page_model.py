#!/usr/bin/env python3
"""chain_page_model.py - the original's side of the chain page D_007635C0,
for the reference test (tools/test_chain_page_reference.py) and the level
smoke (check_chain_page). docs/CHAIN_PAGE.md.

What it models, from the original's own data and instructions:

- the DMA walk of the spliced chain from 001CB800's start tag (base =
  D_0028F700 + index * 0x70000 + 0x1F3EC0 + (a1 << 6)) up to the page's end
  link (base + 0x20): CNT / NEXT / REF / CALL / RET, tags not transferred
  (every VIF code of the page sits in the transferred data);
- VIF1 over that stream: STCYCL, BASE, OFFSET, STMASK, STMOD, FLUSH*,
  MPG, UNPACK V4-32 / V1-32, MSCAL and DIRECT;
- MSCAL executes the ORIGINAL VU1 microcode the stream uploaded (its MPG
  bytes come from the boot ELF through the stream itself) on VuOracle: the
  shadow test's VU1 machine (issue, stalls, 4-cycle MAC / clip flag
  latency, 7-cycle Q) with the VU0 lane rules of ee_float_model for every
  arithmetic lane (DAZ, truncation, FTZ, finite overflow to +-MAX); an
  exponent-255 operand on a live lane raises (not established), like the
  native translations' fault;
- the GIF packets (DIRECT = PATH2, XGKICK = PATH1): PACKED tags with A+D
  and the vertex registers, decoded into GS register writes;
- the GS vertex queue: which vertices form a drawn primitive for each PRIM
  type, under ADC.

Anything outside the shapes the captured pages hold raises (the native
consumer faults on the same). Nothing disc-derived is stored here.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ee_float_model as fm  # noqa: E402
import test_shadow_original_reference as sh  # noqa: E402

M32 = 0xFFFFFFFF
TABLE = 0x7635C0                  # D_007635C0
ARENA = 0x28F700                  # D_0028F700
START = ARENA + 0x1F3EC0          # 001CB800's base for buffer 0, a1 = 0
BUFFER_STRIDE = 0x70000

# The VU1 programs the page CALLs, by the address of their DMA packet (the
# address word of the 001CB760 block). Each packet is one CNT whose VIF
# codes set up VIF1 and upload the program and its constant rows, then RET.
PROGRAM_LANE = 0x233290           # D_00233290: 001F0720's lanes
PROGRAM_SPRITE = 0x231770         # table 0x231770: 001CFBE0 kinds 1 and 5
PROGRAM_SNOW = 0x233800           # D_00233800: 001CFFE0 (kind 1, variant 3), the weather
PROGRAM_STREAK = 0x230800         # table 0x230800: 001CFBE0 kinds 0 and 4 (the impact effect 0x80000060)
PROGRAM_KIND2 = 0x232540          # table 0x232540: 001CFBE0 kind 2 (the cable's hit effect node 0021AAC0)
# Their MPG uploads: (source address of the code, instructions, micro address).
LANE_MPG = (0x2332B8, 138, 0)
SPRITE_MPG = ((0x231798, 256, 0), (0x231FA0, 79, 0x100))
SNOW_MPG = ((0x233828, 256, 0), (0x234030, 81, 0x100))
STREAK_MPG = ((0x230828, 256, 0), (0x231030, 126, 0x100))
KIND2_MPG = ((0x232568, 256, 0), (0x232D70, 66, 0x100))
PROGRAMS = (PROGRAM_LANE, PROGRAM_SPRITE, PROGRAM_SNOW, PROGRAM_STREAK, PROGRAM_KIND2)
# Each packet's bytes: its CNT tag and data, then its RET tag.
PROGRAM_PACKET_SIZE = {PROGRAM_LANE: 0x570, PROGRAM_SPRITE: 0xDD0, PROGRAM_SNOW: 0xDE0, PROGRAM_STREAK: 0xF70,
                       PROGRAM_KIND2: 0xD50}

# The EFU (the streak program's ERCPR / ERLENG, read back by MFP). No
# capture holds an EFU result, so its arithmetic is a model, the one the
# background's ERLENG already uses (em_background_gs.h, docs/BACKGROUND.md):
# ERLENG = 1 / sqrt(x*x + y*y + z*z) and ERCPR = 1 / x, each evaluated
# exactly enough (double; the quotient as VDIV's) and truncated to binary32
# with denormals flushed. The latencies are the VU manual's table, the one
# whose DIV 7 / RSQRT 13 the machine already uses: P is written ERCPR 12 /
# ERLENG 24 cycles after issue; WAITP and a following EFU op stall for it,
# MFP does not (the streak program leaves exactly 12 cycles before each
# MFP that follows an ERCPR, and a WAITP before the one after its ERLENG).
EFU_LATENCY = {0x7BE: 12, 0x73F: 24}            # ERCPR, ERLENG (lower fn)


def efu_trunc(r):
    """A positive double to binary32, truncated, denormals flushed."""
    w = struct.unpack('<I', struct.pack('<f', r))[0]
    if struct.unpack('<f', struct.pack('<I', w))[0] > r:
        w -= 1
    return 0 if w & EXP255 == 0 else w


def efu_ercpr(x):
    """ERCPR P = 1 / x. A zero (or denormal) operand is not established."""
    live(x)
    x = fm._daz(x)
    if fm._is_zero(x):
        raise ModelError('ERCPR of zero (not established)')
    return fm._quotient(0x3F800000, x, False)


def efu_erleng(x, y, z):
    """ERLENG P = 1 / sqrt(x*x + y*y + z*z). A zero length is not established."""
    live(x, y, z)
    a, b, c = (struct.unpack('<f', struct.pack('<I', fm._daz(w)))[0] for w in (x, y, z))
    s = a * a
    s = s + b * b
    s = s + c * c
    if s == 0.0:
        raise ModelError('ERLENG of a zero vector (not established)')
    return efu_trunc(1.0 / __import__('math').sqrt(s))

VIF_NAMES = {0x00: 'NOP', 0x01: 'STCYCL', 0x02: 'OFFSET', 0x03: 'BASE', 0x05: 'STMOD',
             0x10: 'FLUSHE', 0x11: 'FLUSH', 0x13: 'FLUSHA', 0x14: 'MSCAL', 0x20: 'STMASK',
             0x4A: 'MPG', 0x50: 'DIRECT'}


class ModelError(AssertionError):
    pass


def fail(msg):
    raise ModelError(msg)


def u32(b, a):
    return struct.unpack_from('<I', b, a)[0]


# ------------------------------------------------------------ VU1 oracle ---
FIELDS = sh.FIELDS
EXP255 = 0x7F800000


def live(*words):
    for w in words:
        if w & EXP255 == EXP255:
            raise ModelError('an exponent-255 operand reached a live VU lane (not established)')


def vadd(x, y):
    live(x, y)
    return fm._vu_add_raw(x, y)


def vsub(x, y):
    live(x, y)
    return fm._vu_sub_raw(x, y)


def vmul(x, y):
    live(x, y)
    return fm._vu_mul_raw(x, y)


def vdiv(a, b):
    """VDIV on finite operands (every form agrees there; the forms differ
    only in how a NaN divisor passes, and live() refuses exponent 255):
    DAZ, a zero divisor gives +-MAX by the XOR of the signs, else the
    truncated quotient."""
    live(a, b)
    a, b = fm._daz(a), fm._daz(b)
    if fm._is_zero(b):
        return ((a ^ b) & 0x80000000) | 0x7F7FFFFF
    return fm._quotient(a, b, False)


class VuOracle(sh.VU1):
    """The original VU1 microcode on sh.VU1's machine with EE lane rules.
    ACC, Q, I and the R register hold binary32 words; the MAC flags of every
    flag-setting op are computed from the result words (Z: exponent 0,
    S: the sign bit) and reach flag reads 4 cycles later."""

    def __init__(self, elf=None):
        super().__init__(elf)
        self.accw = [0] * 4
        self.q = 0
        self.iw = 0
        self.r = 0
        self.mac = 0
        self.p = 0
        self.p_ready = 0

    @staticmethod
    def lower_reads(lo):
        if lo >> 25 == 0x40 and (lo & 0x7FF) == 0x7BE:              # ERCPR fs.fsf
            return [(lo >> 11 & 31, lo >> 21 & 3)]
        if lo >> 25 == 0x40 and (lo & 0x7FF) == 0x73F:              # ERLENG fs.xyz
            return [(lo >> 11 & 31, c) for c in range(3)]
        return sh.VU1.lower_reads(lo)

    # -- upper
    def upper(self, pc, up):
        code, fs, ft, fd, mask = up & 0x7FF, up >> 11 & 31, up >> 16 & 31, up >> 6 & 31, up >> 21 & 15
        op, x, y, acc, m = up & 63, self.v[fs], self.v[ft], self.accw, FIELDS(mask)
        if code == 0x2FF:
            return []
        if code == 0x1FF:                                    # CLIP fs.xyz against |ft.w|
            xs, w = list(map(sh.vnum, x)), abs(sh.vnum(y[3]))
            f = 0
            for c in range(3):
                if xs[c] > w:
                    f |= 1 << (2 * c)
                if xs[c] < -w:
                    f |= 2 << (2 * c)
            self.later(4, 'cf', f)
            return []
        res, to_acc, flags, dest = {}, False, True, fd
        if (up & 0x3C) == 0x3C:
            if code in (0x17C, 0x17D):                       # FTOI0 / FTOI4
                res = {c: fm.vu_ftoi(x[c], 0 if code == 0x17C else 4) for c in m}
                dest, flags = ft, False
            elif code == 0x13C:                              # ITOF0
                res = {c: fm.vu_itof(x[c], 0) for c in m}
                dest, flags = ft, False
            elif code == 0x1FD:                              # ABS
                res = {c: fm.vu_abs(x[c]) for c in m}
                dest, flags = ft, False
            elif 0x1BC <= code <= 0x1BF:                     # MULAbc
                res = {c: vmul(x[c], y[code & 3]) for c in m}
                to_acc = True
            elif 0x0BC <= code <= 0x0BF:                     # MADDAbc
                res = {c: vadd(acc[c], vmul(x[c], y[code & 3])) for c in m}
                to_acc = True
            elif 0x03C <= code <= 0x03F:                     # ADDAbc
                res = {c: vadd(x[c], y[code & 3]) for c in m}
                to_acc = True
            elif code == 0x27E:                              # SUBAi
                res = {c: vsub(x[c], self.iw) for c in m}
                to_acc = True
            else:
                fail(f'VU upper special {up:#010x} at {pc:#x}')
        elif op < 4:
            res = {c: vadd(x[c], y[op]) for c in m}
        elif op < 8:
            res = {c: vsub(x[c], y[op & 3]) for c in m}
        elif op < 12:
            res = {c: vadd(acc[c], vmul(x[c], y[op & 3])) for c in m}
        elif op < 16:
            res = {c: vsub(acc[c], vmul(x[c], y[op & 3])) for c in m}
        elif op < 20:
            res, flags = {c: fm.vu_max(x[c], y[op & 3]) for c in m}, False
        elif op < 24:
            res, flags = {c: fm.vu_min(x[c], y[op & 3]) for c in m}, False
        elif op < 28:
            res = {c: vmul(x[c], y[op & 3]) for c in m}
        elif op == 0x1C:
            res = {c: vmul(x[c], self.q) for c in m}
        elif op == 0x1E:
            res = {c: vmul(x[c], self.iw) for c in m}
        elif op == 0x1F:
            res, flags = {c: fm.vu_min(x[c], self.iw) for c in m}, False
        elif op == 0x20:
            res = {c: vadd(x[c], self.q) for c in m}
        elif op == 0x22:
            res = {c: vadd(x[c], self.iw) for c in m}
        elif op == 0x23:
            res = {c: vadd(acc[c], vmul(x[c], self.iw)) for c in m}
        elif op == 0x28:
            res = {c: vadd(x[c], y[c]) for c in m}
        elif op == 0x2C:
            res = {c: vsub(x[c], y[c]) for c in m}
        else:
            fail(f'VU upper {up:#010x} at {pc:#x}')
        if flags:
            mac = 0
            for c, w in res.items():
                if w & EXP255 == 0:
                    mac |= 1 << (3 - c)
                if w >> 31:
                    mac |= 1 << (7 - c)
            self.later(4, 'mac', mac)
        if to_acc:
            for c, w in res.items():
                acc[c] = w
            return []
        return [(dest, c, w) for c, w in res.items()] if dest else []

    def settle(self):
        due = sorted(p for p in self.pending if p[0] <= self.cycle)
        for p in due:
            _, _, kind, value = p
            if kind == 'cf':
                self.cf = ((self.cf << 6) | value) & 0xFFFFFF
            elif kind == 'mac':
                self.mac = value
            elif kind == 'q':
                self.q = value
            elif kind == 'p':
                self.p = value
            self.pending.remove(p)

    # -- lower
    def lower(self, pc, lo):
        op = lo >> 25
        if op == 0x40:
            fn = lo & 0x7FF
            it, iss, mask = lo >> 16 & 31, lo >> 11 & 31, lo >> 21 & 15
            if fn == 0x3BC:                                   # DIV Q = fs.fsf / ft.ftf
                fsf, ftf = lo >> 21 & 3, lo >> 23 & 3
                a, b = self.v[iss][fsf], self.v[it][ftf]
                live(a, b)
                self.later(7, 'q', vdiv(a, b))
                self.q_ready = self.cycle + 7
                return None
            if fn == 0x43E:                                   # RINIT R = fs.fsf mantissa
                self.r = self.v[iss][lo >> 21 & 3] & 0x7FFFFF
                return None
            if fn == 0x43F:                                   # RXOR
                self.r ^= self.v[iss][lo >> 21 & 3] & 0x7FFFFF
                return None
            if fn == 0x43C:                                   # RNEXT ft.dest = 1.R'
                self.r = ((self.r << 1) ^ ((self.r >> 22 ^ self.r >> 4) & 1)) & 0x7FFFFF
                for c in FIELDS(mask):
                    if it:
                        self.v[it][c] = 0x3F800000 | self.r
                        self.ready[it][c] = self.cycle + 4
                return None
            if fn == 0x7BE:                                   # ERCPR P = 1 / fs.fsf
                self.later(EFU_LATENCY[fn], 'p', efu_ercpr(self.v[iss][lo >> 21 & 3]))
                self.p_ready = self.cycle + EFU_LATENCY[fn]
                return None
            if fn == 0x73F:                                   # ERLENG P = 1 / |fs.xyz|
                if mask != 0xE:
                    fail(f'ERLENG {lo:#010x} at {pc:#x}: not the xyz form')
                self.later(EFU_LATENCY[fn], 'p', efu_erleng(*self.v[iss][:3]))
                self.p_ready = self.cycle + EFU_LATENCY[fn]
                return None
            if fn == 0x67C:                                   # MFP ft.dest = P
                for c in FIELDS(mask):
                    if it:
                        self.v[it][c] = self.p
                        self.ready[it][c] = self.cycle + 4
                return None
            if fn == 0x7BF:                                   # WAITP (the stall is in run)
                return None
            if fn in (0x3BD, 0x3BE, 0x43D):
                fail(f'VU lower {lo:#010x} at {pc:#x} (not in the page programs)')
        if op == 0x1A:                                        # FMAND vi_t = vi_s & MAC
            it, iss = lo >> 16 & 15, lo >> 11 & 15
            self.vi[it] = self.vi[iss] & self.mac
            return None
        if op in (0x16, 0x18, 0x1B):
            fail(f'VU flag op {lo:#010x} at {pc:#x} (not in the page programs)')
        return super().lower(pc, lo)

    def run(self, entry):
        """sh.VU1.run with the I register kept as a word."""
        pc, branch, end_after = entry & 0x3FFF, None, None
        for _ in range(400000):
            lo, up = struct.unpack_from('<II', self.code, pc & 0x3FFF)
            reads = self.upper_reads(up) + ([] if up >> 31 else self.lower_reads(lo))
            wait = max([self.ready[r][c] for r, c in reads if r] + [self.cycle])
            if not up >> 31 and lo >> 25 == 0x40 and (lo & 0x7FF) in (0x3BF, 0x3BC, 0x3BD, 0x3BE):
                wait = max(wait, self.q_ready)
            if not up >> 31 and lo >> 25 == 0x40 and (lo & 0x7FF) in (0x7BF, 0x7BE, 0x73F):
                wait = max(wait, self.p_ready)                # WAITP / an EFU op on a busy EFU
            self.cycle = wait
            self.settle()
            nxt = branch if branch is not None else pc + 8
            branch = None
            writes = self.upper(pc, up)
            if up >> 31:
                self.iw = lo
            else:
                b = self.lower(pc, lo)
                if b is not None:
                    branch = b
            for r, c, word in writes:
                self.v[r][c] = word
                self.ready[r][c] = self.cycle + 4
            self.vi[0] = 0
            self.v[0] = [0, 0, 0, 0x3F800000]
            self.executed += 1
            self.cycle += 1
            if end_after:
                self.resume = nxt
                return
            if up >> 30 & 1:
                end_after = True
            pc = nxt & 0x3FFF
        fail('VU runaway')


# ------------------------------------------------------------------- GIF ---
def gif_writes(data, path):
    """GIF packets (PACKED only; REGLIST / IMAGE raise) -> the GS register
    writes in order: [(reg, lo64, hi64)] for the PACKED registers, with A+D
    written as ('AD', target register, value) and ('TAG', 0, 0) at the start
    of every GIF tag. Returns (writes, qwords used)."""
    out, q, n = [], 0, len(data) // 16
    while True:
        if q >= n:
            fail(f'{path}: GIF packet runs past its data')
        lo, hi = struct.unpack_from('<QQ', data, 16 * q)
        q += 1
        nloop, eop, pre, prim, flg = lo & 0x7FFF, lo >> 15 & 1, lo >> 46 & 1, lo >> 47 & 0x7FF, lo >> 58 & 3
        nreg = (lo >> 60) or 16
        if flg != 0:
            fail(f'{path}: GIF FLG {flg} (only PACKED occurs in the page)')
        out.append(('TAG', 0, 0))
        if pre:
            out.append(('AD', 0x00, prim))
        regs = [(hi >> (4 * r)) & 15 for r in range(nreg)]
        for _ in range(nloop):
            for r in regs:
                if q >= n:
                    fail(f'{path}: GIF data runs past the packet')
                a, b = struct.unpack_from('<QQ', data, 16 * q)
                q += 1
                if r == 0x0E:
                    out.append(('AD', b & 0xFF, a))
                else:
                    out.append((r, a, b))
        if eop:
            return out, q


# The A+D writes the captured pages hold, and the drawing state they set.
AD_STATE = {0x06: 'TEX0_1', 0x08: 'CLAMP_1', 0x14: 'TEX1_1', 0x42: 'ALPHA_1', 0x46: 'COLCLAMP',
            0x47: 'TEST_1'}
AD_NOOP = {0x3F: 'TEXFLUSH'}
STATE_ORDER = ('TEX0_1', 'CLAMP_1', 'TEX1_1', 'ALPHA_1', 'TEST_1', 'COLCLAMP')
VERTS_PER_PRIM = {0: 1, 1: 2, 2: 2, 3: 3, 4: 3, 5: 3, 6: 2}


class Gs:
    """The GS drawing state and vertex queue the page's packets drive.
    Every drawn primitive is recorded as (prim register, state tuple,
    vertices), a vertex being (x, y, z, f or None, (r, g, b, a), q, s, t,
    u, v, q_known). The GS's internal Q (the one PACKED ST holds for the
    next PACKED RGBAQ) is 1.0 at the start of every GIF tag (measured in
    PCSX2's software GS: p8_gif pk_q_after_packed_st, docs/GS_EXACT.md 2.1),
    so every vertex's Q is known (q_known 1); the page never takes a Q from
    the frame's earlier draws."""

    def __init__(self):
        self.state = {}
        self.prim = 0
        self.rgba = (0, 0, 0, 0)
        self.rq, self.rq_known = 0, 0
        self.q, self.q_known = 0, 0
        self.st = (0, 0)
        self.uv = (0, 0)
        self.queue = []
        self.prims = []

    def write(self, reg, a, b, path):
        w = struct.unpack('<4I', struct.pack('<QQ', a, b))
        if reg == 0x01:                                     # PACKED RGBAQ: Q from the held Q
            self.rgba = (w[0] & 0xFF, w[1] & 0xFF, w[2] & 0xFF, w[3] & 0xFF)
            self.rq, self.rq_known = self.q, self.q_known
        elif reg == 0x02:                                   # PACKED ST (holds Q)
            self.st, self.q, self.q_known = (w[0], w[1]), w[2], 1
        elif reg == 0x03:                                   # PACKED UV
            self.uv = (w[0] & 0x3FFF, w[1] & 0x3FFF)
        elif reg in (0x04, 0x05):                           # PACKED XYZF2 / XYZ2
            x, y = w[0] & 0xFFFF, w[1] & 0xFFFF
            if reg == 0x04:
                z, f = (w[2] >> 4) & 0xFFFFFF, (w[3] >> 4) & 0xFF
            else:
                z, f = w[2], None
            adc = (w[3] >> 15) & 1
            self.vertex((x, y, z, f, self.rgba, self.rq, self.st[0], self.st[1], self.uv[0],
                         self.uv[1], self.rq_known), adc)
        elif reg == 0x06:                                   # PACKED TEX0_1: the low 64 bits
            self.state['TEX0_1'] = a
        elif reg == 0x0F:
            pass
        else:
            fail(f'{path}: PACKED register {reg:#x} (not in the page)')

    def tag(self):
        """A GIF tag starts: the Q a PACKED RGBAQ takes is 1.0 again."""
        self.q, self.q_known = 0x3F800000, 1

    def ad(self, reg, value, path):
        if reg in AD_STATE:
            self.state[AD_STATE[reg]] = value
        elif reg == 0x00:
            self.prim = value & 0x7FF
            self.queue = []
        elif reg in AD_NOOP:
            pass
        else:
            fail(f'{path}: A+D register {reg:#x} (not in the page)')

    def vertex(self, v, adc):
        t = self.prim & 7
        if t == 7:
            fail('GS PRIM type 7')
        need = VERTS_PER_PRIM[t]
        self.queue.append(v)
        if t in (0, 1, 3, 6):                               # lists
            if len(self.queue) == need:
                if not adc:
                    self.emit(list(self.queue))
                self.queue = []
        elif t in (2, 4):                                   # strips
            if len(self.queue) > need:
                self.queue.pop(0)
            if len(self.queue) == need and not adc:
                self.emit(list(self.queue))
        else:                                               # fan: the first stays
            if len(self.queue) > need:
                self.queue.pop(1)
            if len(self.queue) == need and not adc:
                self.emit(list(self.queue))

    def emit(self, verts):
        self.prims.append((self.prim, tuple(self.state.get(k) for k in STATE_ORDER), tuple(verts)))


def gs_feed(gs, data, path):
    writes, used = gif_writes(data, path)
    for reg, a, b in writes:
        if reg == 'TAG':
            gs.tag()
        elif reg == 'AD':
            gs.ad(a, b, path)
        else:
            gs.write(reg, a, b, path)
    return used


# ------------------------------------------------------------------- page ---
class Page:
    """One page walk. read(address, size) -> bytes of original memory (the
    capture's RAM, or the port's page dump). elf: the boot ELF bytes (the
    oracle's instruction fetch comes from the MPG uploads in the stream)."""

    def __init__(self, read, elf=None, skip_calls=(), units=None):
        self.read = read
        # Class-2 object units walked over (001CABA0's CALLs, CHAIN_PAGE.md
        # section 6): {target: (last TEX0, last PRIM)}. The unit's GS state
        # REF (001D1F80(3, 2, 2): set 2 class 2) and its last kicked TEX0 /
        # PRIM hold for what follows; its triangles are compared elsewhere.
        self.units = dict(units or {})
        self.vu = VuOracle(elf)
        self.base = self.offset = self.tops = self.dbf = 0
        self.cl = self.wl = 1
        self.cycle_set = False
        self.cycle_inherited = 0
        self.gs = Gs()
        self.kicks = []            # (program, top, gif bytes)
        self.directs = []          # (source address, gif bytes)
        self.program = None        # the packet address whose MPG is loaded
        self.parts = 0
        self.first = None          # the first MPG part's source address
        self.mpg = []
        self.known = bytearray(1024)
        self.unknown_reads = 0
        self.transfers = []
        self.skip_calls = set(skip_calls)
        self.skipped = []
        self.mscal_counts = {}     # program -> MSCALs run

    def u(self, a):
        return u32(self.read(a, 4), 0)

    def dma(self, start):
        """[(source address of the data, bytes)] in DMA order, from 001CB800's
        start tag to the page's end link."""
        out, stack, cur, end = [], [], start, start + 0x20
        for _ in range(100000):
            if cur == end and not stack:
                return out
            lo, hi = self.u(cur), self.u(cur + 4)
            qwc, tid, addr = lo & 0xFFFF, lo >> 28 & 7, hi & 0x0FFFFFFF
            if lo & 0x8C000000:
                fail(f'DMA tag {cur:#x}: {lo:#010x} carries PCE / IRQ bits')
            if tid == 1:                                    # CNT
                data, nxt = cur + 16, cur + 16 + 16 * qwc
            elif tid == 2:                                  # NEXT
                data, nxt = cur + 16, addr
            elif tid == 3:                                  # REF
                data, nxt = addr, cur + 16
            elif tid == 5:                                  # CALL
                if len(stack) >= 2:
                    fail(f'DMA CALL at {cur:#x}: a third nesting level')
                if (addr in self.skip_calls or addr in self.units) and not stack:
                    self.skipped.append((cur, addr))
                    data, nxt = cur + 16, cur + 16 + 16 * qwc
                    if qwc:
                        out.append((data, self.read(data, 16 * qwc)))
                    if addr in self.units:
                        out.append((None, addr))
                    cur = nxt
                    continue
                data = cur + 16
                stack.append(cur + 16 + 16 * qwc)
                nxt = addr
            elif tid == 6:                                  # RET
                if not stack:
                    fail(f'DMA RET at {cur:#x} with an empty stack')
                data, nxt = cur + 16, stack.pop()
            else:
                fail(f'DMA tag {cur:#x}: ID {tid} (not in the page)')
            if qwc:
                out.append((data, self.read(data, 16 * qwc)))
            self.transfers.append((cur, tid, qwc, addr))
            cur = nxt
        fail('DMA runaway')

    def run(self, start):
        stream = self.dma(start)
        # one flat VIF stream with each word's source address
        words = []
        for src, data in stream:
            if src is None:                                 # a class-2 unit's CALL
                words.append((None, data))
                continue
            for i in range(0, len(data), 4):
                words.append((src + i, u32(data, i)))
        i, n = 0, len(words)
        while i < n:
            at, v = words[i]
            i += 1
            if at is None:
                tex0, prim = self.units[v]
                self.gs.state.update({'TEX0_1': tex0, 'CLAMP_1': 0, 'TEX1_1': 0x60, 'ALPHA_1': 0x8000000068,
                                      'TEST_1': 0x53001, 'COLCLAMP': 1})
                self.gs.prim, self.gs.queue = prim, []
                self.cycle_set, self.cl, self.wl = True, 4, 4
                self.program, self.parts = None, 0
                continue
            cmd, num, imm = v >> 24 & 0x7F, v >> 16 & 0xFF, v & 0xFFFF
            if v >> 31:
                fail(f'VIF code {v:#010x} at {at:#x}: interrupt bit')
            if cmd >= 0x60:
                vn, vl, cnt = cmd >> 2 & 3, cmd & 3, num or 256
                if cmd & 0x10 or vl != 0 or vn not in (0, 3) or imm & 0xC000:
                    fail(f'VIF UNPACK {v:#010x} at {at:#x} (only V4-32 / V1-32 without mask, FLG, USN occur)')
                if not self.cycle_set:
                    # The cycle is the frame's (the lane program packet's
                    # STCYCL sits in its DMA tag, which is not transferred):
                    # taken as CL == WL, the only setting under which the
                    # program finds its constant rows 0..13 in one block.
                    self.cycle_inherited += 1
                elif self.wl == 0 or self.wl > self.cl:
                    fail(f'VIF UNPACK at {at:#x} with the cycle {self.cl}/{self.wl}')
                comps = vn + 1
                if i + comps * cnt > n:
                    fail('VIF UNPACK runs past the stream')
                dst = imm & 0x3FF
                for k in range(cnt):
                    d = ((dst + (k // self.wl) * self.cl + k % self.wl) if self.cycle_set else dst + k) & 1023
                    q = list(struct.unpack_from('<4I', self.vu.mem, 16 * d))
                    for c in range(4):
                        q[c] = words[i + comps * k + (c if comps == 4 else 0)][1]
                    struct.pack_into('<4I', self.vu.mem, 16 * d, *q)
                    self.known[d] = 1
                i += comps * cnt
                continue
            if cmd == 0x4A:                                 # MPG
                cnt = num or 256
                if i + 2 * cnt > n:
                    fail('VIF MPG runs past the stream')
                code = b''.join(struct.pack('<I', words[i + k][1]) for k in range(2 * cnt))
                self.vu.code[imm * 8:imm * 8 + 8 * cnt] = code
                first = words[i][0]
                self.mpg.append((first, imm, cnt))
                if (first, cnt, imm) == LANE_MPG:
                    self.program, self.parts = PROGRAM_LANE, 1
                elif (first, cnt, imm) in (SPRITE_MPG[0], SNOW_MPG[0], STREAK_MPG[0], KIND2_MPG[0]):
                    self.program, self.parts, self.first = None, 1, first
                elif (first, cnt, imm) == SPRITE_MPG[1] and self.parts == 1 and self.program is None \
                        and self.first == SPRITE_MPG[0][0]:
                    self.program, self.parts = PROGRAM_SPRITE, 2
                elif (first, cnt, imm) == SNOW_MPG[1] and self.parts == 1 and self.program is None \
                        and self.first == SNOW_MPG[0][0]:
                    self.program, self.parts = PROGRAM_SNOW, 2
                elif (first, cnt, imm) == STREAK_MPG[1] and self.parts == 1 and self.program is None \
                        and self.first == STREAK_MPG[0][0]:
                    self.program, self.parts = PROGRAM_STREAK, 2
                elif (first, cnt, imm) == KIND2_MPG[1] and self.parts == 1 and self.program is None \
                        and self.first == KIND2_MPG[0][0]:
                    self.program, self.parts = PROGRAM_KIND2, 2
                else:
                    fail(f'MPG of {cnt} instructions from {first:#x} to micro {imm:#x} (not a page program)')
                i += 2 * cnt
                continue
            if cmd == 0x50:                                 # DIRECT
                cnt = imm or 65536
                while i < n and words[i][0] & 0xF:
                    fail(f'VIF DIRECT at {at:#x} not followed by an aligned qword')
                data = b''.join(struct.pack('<I', words[i + k][1]) for k in range(4 * cnt))
                used = gs_feed(self.gs, data, f'DIRECT at {at:#x}')
                if used != cnt:
                    fail(f'DIRECT at {at:#x}: {cnt} qwords, the GIF packet uses {used}')
                self.directs.append((words[i][0], data))
                i += 4 * cnt
                continue
            if cmd == 0x20:                                 # STMASK: one data word
                i += 1
                continue
            if cmd == 0x01:
                self.cl, self.wl, self.cycle_set = imm & 0xFF, imm >> 8 & 0xFF, True
                continue
            if cmd == 0x03:
                self.base = imm & 0x3FF
                continue
            if cmd == 0x02:
                self.offset, self.dbf, self.tops = imm & 0x3FF, 0, self.base
                continue
            if cmd == 0x05:
                if imm & 3:
                    fail(f'VIF STMOD {imm} (only 0 occurs)')
                continue
            if cmd == 0x14:                                 # MSCAL
                self.mscal(imm, at)
                continue
            if cmd in (0x00, 0x10, 0x11, 0x13):
                continue
            fail(f'VIF code {v:#010x} at {at:#x} (not in the page)')
        return self

    def program_of(self, code_address):
        for p in PROGRAMS:
            if p <= code_address < p + PROGRAM_PACKET_SIZE[p]:
                return p
        return None

    def mscal(self, imm, at):
        if self.program is None or imm != 0:
            fail(f'MSCAL {imm:#x} at {at:#x}: no page program loaded, or not entry 0')
        self.mscal_counts[self.program] = self.mscal_counts.get(self.program, 0) + 1
        top = self.tops
        self.dbf ^= 1
        self.tops = self.base + (self.offset if self.dbf else 0)
        vu = self.vu
        vu.kicks, vu.events, vu.top, vu.watch = [], [], top, set()
        vu.pending, vu.cycle, vu.q_ready, vu.p_ready = [], 0, 0, 0
        vu.ready = [[0] * 4 for _ in range(32)]
        vu.run(imm * 8)
        for e in vu.events:
            if e[0] == 'kick':
                raw = e[3]
                self.kicks.append((self.program, e[1], raw))
                gs_feed(self.gs, raw, f'XGKICK {e[1]:#x} of {self.program:#x}')


def weather_lists(read, transfers):
    """The CALL targets among `transfers` (Page.dma's) that are 001E0D70's
    kick of the weather's channel-3 list (context +0x2520, CALLed at slot
    0xFFB): lists whose first block is 001CFFE0's, a REF of 8 qwords (the
    001CB9B0 blend preset) and then a CALL of the snow program D_00233800."""
    out = set()
    for (_cur, tid, _q, a) in transfers:
        if tid != 5 or not ARENA <= a < 0x800000:
            continue
        t0, t1 = read(a, 16), read(a + 16, 16)
        if u32(t0, 0) >> 28 & 7 == 3 and u32(t0, 0) & 0xFFFF == 8 and u32(t1, 0) >> 28 & 7 == 5 \
                and u32(t1, 4) & 0x0FFFFFFF == PROGRAM_SNOW:
            out.add(a)
    return out


def arena_skips(read, start, weather=True):
    """The top-level CALLs into the packet arena a captured page holds that
    the port does not run on the page (the object units 001CAAC0 sorts,
    001DDE10's four-sprite pass), and, when `weather` is false, the
    weather's kick too (walked over: a quick run that does not execute the
    108 snow MSCALs of every capture)."""
    probe = Page(read)
    probe.dma(start)
    calls = {a for (_c, tid, _q, a) in probe.transfers if tid == 5 and ARENA <= a < 0x800000}
    keep = weather_lists(read, probe.transfers) if weather else set()
    return sorted(calls - keep)


def ram_reader(ram):
    def read(a, n):
        if a < 0 or a + n > len(ram):
            fail(f'read {a:#x}+{n:#x} outside RAM')
        return bytes(ram[a:a + n])
    return read


def page_start(index, a1=0):
    """001CB800's base for the D_00810E80 index and a1."""
    return START + index * BUFFER_STRIDE + (a1 << 6)


def latest_start(ram):
    """The start tag of the page a capture's last kick spliced: 001CB800
    stores its base at the context's +0 (its a2 = D_00275670's block), and
    the next frame's 001CB8A0 has not run at the route snapshots (the page
    it would reset is walkable in every capture)."""
    ctx = u32(ram, 0x275670)
    return u32(ram, ctx)
