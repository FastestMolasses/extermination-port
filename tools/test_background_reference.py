#!/usr/bin/env python3
"""Check the AREA11 level background against the original lists and code.

FIRST_LEVEL_AUDIT R09, docs/BACKGROUND.md. The original never clears the
colour buffer in a world frame; it draws a full-screen textured grid
behind the level. Evidence chain, all read from the owner's files (no
original bytes are embedded here):

1. Frame clear (R09). In every AREA11 world capture both display lists
   (0x0028F700 / 0x00293700) start with the draw-env REF (25 qwords) and
   the 001D2300 clear REF: a sprite over the whole 512x224 field whose
   TEST_1 has ATE 1, ATST NEVER, AFAIL ZB_ONLY, so the GS writes only Z.
   The third tag CALLs render channel 3's list, the node 001E1E60 returned
   (ctx+0x1D8). The status-hub capture has no such CALL.
2. Channel 3's list is replayed as the DMAC/VIF1/GIF would: the GS state
   at its single MSCAL (kernel packet 0x0023C990, MSCAL 0) must equal the
   exported asset (TEX0 = ctx+0x1D0, CLAMP_1, TEX1_1, RGBAQ =
   int(128 * ctx+0x1C0..), PRIM of the uploaded GIF tag D_00253560,
   TEST_1, ZBUF_1) and carry ZTST ALWAYS, no pixel-dropping alpha test and
   ZMSK 1 (the backend's depth-off state; other values are refused).
3. The asset's grid constants equal the ELF's D_002535B0..EF and the
   uploaded dmem 0x204..0x207; dmem 0x206.z (D_002535B8) equals the zoom
   ctx+0x2468.
4. em_background_gs_matrix (src/gfx/metal/em_background_gs.h) applied to
   the captured view copy ctx+0x2380 reproduces the uploaded dmem
   0x200..0x203 (= D_00253570) bit for bit. The draw receives the port's
   native view and negates rows 1 and 2 to get ctx+0x2380; that the native
   view equals the original with those rows negated is the
   em_cs_view_to_native convention checked by
   tools/test_census_standins_reference.py, which this path depends on (it is
   not re-proved here).
5. The ORIGINAL VU1 kernel, decoded from the ELF's MPG packet and executed
   over the captured upload, kicks 31 strips of 64 vertices; every ST and
   XYZ2 field must equal the native grid. ERLENG is evaluated by the same
   model in both (1/sqrt in double, truncated); see docs/BACKGROUND.md.
   EM_TEST_FULL=1 adds a sweep of synthetic views and zooms.
6. The asset texels are the disc's: the area's level-load GS upload
   (INDEX.IDX sector area+4, 001FFCD0 -> 001FF590(0xAB, 1) -> 00200830 VIF1
   chain) is replayed from the user's disc (--iso, default the decomp's
   Extermination-rebuilt.iso, or --disc) with the decomp exporter's
   replay, and the TEX0 texture decoded from it (PSMT8 + CSM1 CLUT) must
   equal the asset and the texture decoded from every captured GS freeze.
7. em_background_gs_ndc maps GS field pixel i's footprint [1792 + i,
   1793 + i) onto the i-th 1/512 of the viewport (Y: 1936 + j over 224
   lines), the convention of the port's world projection (em_math.h:
   x_gs - 2048 = 256 * ndc_x, y_gs - 2048 = -112 * ndc_y), and the grid the
   original kernel kicks spans X 1792..2303.9375 / Y 1936..2159.9375 (12.4),
   so it covers every GS sample point of the field; natively the last
   1/16 field pixel at the right and bottom edges is uncovered, which can
   leave a Metal pixel centre uncovered only above 8x upscale.

With --native BMP (a native EM_CAPTURE of the first-control frame) and
--original PNG it also reports the sky-region metric: black pixels and
the mean colour of the region the original shows as background.
"""
import argparse
import ctypes as C
import hashlib
import json
import math
import random
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from reference_mode import MODE, FULL, banner, part  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
CAPTURES = ('playable_ee.bin', 'opening_ee.bin', 'handoff_ee.bin',
            'roger-encounter/eeMemory.bin', 'elevator/clip47_ee.bin',
            'elevator/completed_ee.bin')
HUB = 'status-hub/eeMemory.bin'
GS_FREEZES = ('opening_gs.bin', 'roger-encounter/gs.bin', 'status-hub/gs.bin')
LIST_HEADS = (0x28F700, 0x293700)
CTX_PTR, CTX_PACKETS = 0x275670, 0x275674
KERNEL_PACKET, GRID_CONSTANTS, TEMPLATE = 0x23C990, 0x2535B0, 0x253560
GRID = 32


def fail(msg):
    raise SystemExit('FAIL: ' + msg)


def check(cond, msg):
    if not cond:
        fail(msg)


def u32(b, a): return struct.unpack_from('<I', b, a)[0]
def u64(b, a): return struct.unpack_from('<Q', b, a)[0]
def bits(x): return struct.unpack('<I', struct.pack('<f', x))[0]
def flt(b): return struct.unpack('<f', struct.pack('<I', b & 0xffffffff))[0]


def trunc(x):
    """binary32 with the VU's round-toward-zero (em_fog_gs_vu_trunc)."""
    r = flt(bits(x))
    if abs(r) > abs(x):
        r = flt(bits(r) - 1)
    return r


# --- DMA / VIF1 / GIF --------------------------------------------------------

def dma_walk(ram, start, limit=100000):
    a, stack = start, []
    for _ in range(limit):
        w0, addr = struct.unpack_from('<2I', ram, a)
        tid, qwc = (w0 >> 28) & 7, w0 & 0xFFFF
        if tid == 1:
            yield tid, qwc, addr, a + 16; a += 16 * (qwc + 1)
        elif tid == 2:
            yield tid, qwc, addr, a + 16; a = addr
        elif tid in (3, 4):
            yield tid, qwc, addr, addr; a += 16
        elif tid == 5:
            yield tid, qwc, addr, a + 16
            stack.append(a + 16 * (qwc + 1)); a = addr
        elif tid == 6:
            yield tid, qwc, addr, a + 16
            if not stack: return
            a = stack.pop()
        else:
            yield tid, qwc, addr, a + 16
            return
    fail(f'DMA chain at {start:#x} does not end')


def replay(ram, node):
    """GS registers, dmem and kernel packets of one channel list, with the
    state snapshotted at each MSCAL."""
    stream, calls = bytearray(), []
    for tid, qwc, addr, data in dma_walk(ram, node):
        if tid == 5: calls.append(addr)
        if qwc: stream += ram[data:data + 16 * qwc]
    s, i, regs, dmem, kicks, cl, wl = bytes(stream), 0, {}, {}, [], 4, 4
    while i + 4 <= len(s):
        code = u32(s, i); i += 4
        cmd, num, imm = (code >> 24) & 0x7F, (code >> 16) & 0xFF, code & 0xFFFF
        if cmd == 0x01: cl, wl = imm & 0xFF, imm >> 8
        elif cmd in (0x14, 0x15, 0x17): kicks.append((imm, dict(regs), dict(dmem)))
        elif cmd == 0x20: i += 4
        elif cmd in (0x30, 0x31): i += 16
        elif cmd == 0x4A: i += 8 * (num or 256)
        elif cmd in (0x50, 0x51):
            end, pos = i + 16 * (imm or 65536), i
            while pos + 16 <= end:
                lo, hi = struct.unpack_from('<QQ', s, pos); pos += 16
                nloop, flg, nreg = lo & 0x7FFF, (lo >> 58) & 3, (lo >> 60) or 16
                check(flg == 0, 'channel list GIF is not PACKED')
                if (lo >> 46) & 1: regs[0] = (lo >> 47) & 0x7FF
                rl = [(hi >> (4 * k)) & 15 for k in range(nreg)]
                for _ in range(nloop):
                    for r in rl:
                        d0, d1 = struct.unpack_from('<QQ', s, pos); pos += 16
                        regs[d1 & 0xFF if r == 0xE else r] = d0
            i = end
        elif cmd >= 0x60:
            vn, vl, cnt = (cmd >> 2) & 3, cmd & 3, num or 256
            check(vn == 3 and vl == 0 and not (imm >> 15) & 1 and wl <= cl,
                  f'unexpected UNPACK {code:#x}')
            for k in range(cnt):
                dmem[((imm & 0x3FF) + (k // wl) * cl + k % wl) & 0x3FF] = \
                    s[i + 16 * k:i + 16 * k + 16]
            i += ((32 >> vl) * (vn + 1) * cnt + 31) // 32 * 4
        elif cmd not in (0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x10, 0x11, 0x13):
            fail(f'unknown VIF code {code:#010x}')
    return calls, kicks


def main_list_head(ram, head):
    out = []
    for n, item in enumerate(dma_walk(ram, head)):
        out.append(item)
        if n == 2: break
    return out


# --- the original VU1 kernel -----------------------------------------------

def kernel_program(elf):
    """VU address -> (lower, upper) of the MPG in the 0x0023C990 packet."""
    def at(va): return va - 0x100000 + 0x300
    w0 = u32(elf, at(KERNEL_PACKET))
    check((w0 >> 28) & 7 == 1, 'kernel packet is not a CNT tag')
    end = KERNEL_PACKET + 16 + 16 * (w0 & 0xFFFF)
    i, prog = KERNEL_PACKET + 16, {}
    check((u32(elf, at(end)) >> 28) & 7 == 6, 'kernel packet does not RET')
    while i < end:
        code = u32(elf, at(i)); i += 4
        cmd, num, imm = (code >> 24) & 0x7F, (code >> 16) & 0xFF, code & 0xFFFF
        if cmd == 0x4A:
            for k in range(num or 256):
                prog[imm + k] = (u32(elf, at(i + 8 * k)), u32(elf, at(i + 8 * k + 4)))
            i += 8 * (num or 256)
        elif cmd == 0x20: i += 4
        elif cmd not in (0x00, 0x01, 0x05, 0x10, 0x11, 0x13):
            fail(f'kernel packet VIF code {code:#x}')
    check(prog, 'no MPG in the kernel packet')
    return prog


def erleng(v):
    """The EFU ERLENG model shared with em_background_gs.h."""
    x, y, z = flt(v[0]), flt(v[1]), flt(v[2])
    return trunc(1.0 / math.sqrt(x * x + y * y + z * z))


def run_kernel(prog, dmem_in, start=0):
    """Execute the decoded kernel; returns the kicked buffers (lists of
    qwords). Only the operations this program uses are decoded; any other
    encoding faults."""
    mem = [list(struct.unpack('<4I', dmem_in.get(a, bytes(16)))) for a in range(1024)]
    vf = [[0, 0, 0, 0] for _ in range(32)]; vf[0] = [0, 0, 0, bits(1.0)]
    vi = [0] * 16; acc = [0.0] * 4; p = 0.0; kicks = []
    pc, branch, end_after = start, None, None
    for _ in range(200000):
        lower, upper = prog[pc]
        nextpc = branch if branch is not None else pc + 1
        branch = None
        # upper
        if upper & 0x7FF != 0x2FF:
            op, mask = upper & 63, (upper >> 21) & 15
            ft, fs, fd = (upper >> 16) & 31, (upper >> 11) & 31, (upper >> 6) & 31
            x = [flt(k) for k in vf[fs]]; y = [flt(k) for k in vf[ft]]
            res, dest, to_acc = None, fd, False
            if op < 4:                       # ADDbc
                res = [trunc(x[c] + y[op]) for c in range(4)]
            elif 8 <= op < 12:               # MADDbc
                res = [trunc(acc[c] + trunc(x[c] * y[op - 8])) for c in range(4)]
            elif 24 <= op < 28:              # MULbc
                res = [trunc(x[c] * y[op - 24]) for c in range(4)]
            elif op == 0x28:                 # ADD
                res = [trunc(x[c] + y[c]) for c in range(4)]
            elif op == 0x29:                 # MADD
                res = [trunc(acc[c] + trunc(x[c] * y[c])) for c in range(4)]
            elif op >= 0x3C:
                bc = op & 3
                if fd == 6:                  # MULAbc
                    res = [trunc(x[c] * y[bc]) for c in range(4)]; to_acc = True
                elif fd == 2:                # MADDAbc
                    res = [trunc(acc[c] + trunc(x[c] * y[bc])) for c in range(4)]; to_acc = True
                elif (upper & 0x7FF) == 0x2BC:   # ADDA
                    res = [trunc(x[c] + y[c]) for c in range(4)]; to_acc = True
                else:
                    fail(f'VU upper special {upper:#x} at {pc}')
            else:
                fail(f'VU upper {upper:#x} at {pc}')
            for c in range(4):
                if mask & (8 >> c):
                    if to_acc: acc[c] = res[c]
                    elif dest: vf[dest][c] = bits(res[c])
        # lower
        if lower != 0x8000033C:
            op = lower >> 25
            it, is_ = (lower >> 16) & 31, (lower >> 11) & 31
            mask = (lower >> 21) & 15
            imm11 = (lower & 0x7FF) - (0x800 if lower & 0x400 else 0)
            if op == 0x00:                   # LQ
                q = mem[(vi[is_] + imm11) & 1023]
                for c in range(4):
                    if mask & (8 >> c): vf[it][c] = q[c]
            elif op == 0x01:                 # SQ (it = base, is = source)
                a = (vi[it] + imm11) & 1023
                for c in range(4):
                    if mask & (8 >> c): mem[a][c] = vf[is_][c]
            elif op in (0x08, 0x09):         # IADDIU / ISUBIU
                imm15 = (lower & 0x7FF) | (((lower >> 21) & 15) << 11)
                vi[it] = (vi[is_] + (imm15 if op == 0x08 else -imm15)) & 0xFFFF
            elif op in (0x28, 0x29, 0x2C):   # IBEQ / IBNE / IBLTZ
                a, b = vi[is_], vi[it]
                sa = a - 0x10000 if a & 0x8000 else a
                take = {0x28: a == b, 0x29: a != b, 0x2C: sa < 0}[op]
                if take: branch = pc + 1 + imm11
            elif op == 0x40:
                fn = lower & 0x7FF
                if fn == 0x6FC:              # XGKICK
                    base = vi[is_]
                    tag = mem[base]
                    nloop = tag[0] & 0x7FFF
                    nreg = (tag[1] >> 28) or 16
                    kicks.append([list(mem[(base + k) & 1023])
                                  for k in range(1 + nloop * nreg)])
                elif fn == 0x73F:            # ERLENG
                    p = erleng(vf[is_])
                elif fn == 0x7BF:            # WAITP
                    pass
                elif fn == 0x67C:            # MFP
                    for c in range(4):
                        if mask & (8 >> c): vf[it][c] = bits(p)
                else:
                    fail(f'VU lower special {lower:#x} at {pc}')
            else:
                fail(f'VU lower {lower:#x} at {pc}')
        vi[0] = 0; vf[0] = [0, 0, 0, bits(1.0)]
        if end_after is not None:
            return kicks
        if upper & 0x40000000:
            end_after = True
        pc = nextpc
    fail('kernel did not end')


def kicked_vertices(kicks):
    """[strip][vertex] = (S bits, T bits, X, Y, Z) from the kicked GIF."""
    strips = []
    for buf in kicks:
        tag = buf[0]
        nloop, nreg = tag[0] & 0x7FFF, (tag[1] >> 28) or 16
        regs = [(tag[2] >> (4 * k)) & 15 for k in range(nreg)]
        check(regs == [2, 5, 2, 5], f'kick GIF regs {regs}')
        verts, k = [], 1
        for _ in range(nloop):
            for pair in range(2):
                st, xyz = buf[k], buf[k + 1]; k += 2
                verts.append((st[0], st[1], xyz[0] & 0xFFFF, xyz[1] & 0xFFFF, xyz[2]))
        strips.append(verts)
    return strips


# --- native -------------------------------------------------------------------

SHIM = r'''
#include "gfx/metal/em_background_gs.h"
int parse(const unsigned char *b, unsigned long n, EmBackgroundGsAsset *a)
{ return em_background_gs_parse(b, n, a); }
const char *unsupported(const unsigned char *b, unsigned long n)
{ EmBackgroundGsAsset a; if (em_background_gs_parse(b, n, &a)) return "parse";
  const char *w = em_background_gs_unsupported(&a); return w ? w : ""; }
void ndc(const unsigned short *xy, float *o) { em_background_gs_ndc(xy, o); }
void matrix(const float *v, float *o) { em_background_gs_matrix(v, o); }
void grid(const unsigned char *b, unsigned long n, const float *m, float zoom,
          float *st, unsigned short *xy)
{
    EmBackgroundGsAsset a;
    static EmBackgroundGsVertex g[EM_BACKGROUND_GS_GRID][EM_BACKGROUND_GS_GRID];
    if (em_background_gs_parse(b, n, &a)) return;
    em_background_gs_grid(&a, m, zoom, g);
    for (unsigned r = 0; r < EM_BACKGROUND_GS_GRID; ++r)
        for (unsigned c = 0; c < EM_BACKGROUND_GS_GRID; ++c) {
            unsigned k = r * EM_BACKGROUND_GS_GRID + c;
            st[2*k] = g[r][c].st[0]; st[2*k+1] = g[r][c].st[1];
            xy[2*k] = g[r][c].xy[0]; xy[2*k+1] = g[r][c].xy[1];
        }
}
'''


class Native:
    def __init__(self, tmp):
        src, lib = Path(tmp) / 'bg.c', Path(tmp) / 'bg.dylib'
        src.write_text(SHIM)
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-ffp-contract=off', '-shared', '-fPIC', '-I' + str(ROOT / 'src'),
                        str(src), '-o', str(lib)], check=True)
        self.lib = C.CDLL(str(lib))
        self.lib.unsupported.restype = C.c_char_p
        self.lib.grid.argtypes = [C.c_char_p, C.c_ulong, C.POINTER(C.c_float), C.c_float,
                                  C.POINTER(C.c_float), C.POINTER(C.c_ushort)]

    def matrix(self, view_bits):
        v = (C.c_uint32 * 16)(*view_bits); o = (C.c_uint32 * 16)()
        self.lib.matrix(v, o); return list(o)

    def grid(self, asset, m_bits, zoom):
        m = (C.c_uint32 * 16)(*m_bits)
        st = (C.c_float * (GRID * GRID * 2))(); xy = (C.c_ushort * (GRID * GRID * 2))()
        self.lib.grid(asset, len(asset), C.cast(m, C.POINTER(C.c_float)),
                      zoom, st, xy)
        return ([bits(v) for v in st], list(xy))


def compare_grid(native, asset, dmem, label):
    """Original kernel vs native grid over one upload. Returns vertices."""
    m_bits = [w for a in range(0x200, 0x204) for w in struct.unpack('<4I', dmem[a])]
    zoom = flt(struct.unpack('<4I', dmem[0x204])[2])     # D_002535B8
    strips = kicked_vertices(run_kernel(PROGRAM, dmem))
    check(len(strips) == GRID - 1 and all(len(s) == 2 * GRID for s in strips),
          f'{label}: kernel kicked {len(strips)} strips')
    xs = [v[2] for s in strips for v in s]; ys = [v[3] for s in strips for v in s]
    # every GS sample point of the field (X 1792..2303, Y 1936..2159) lies
    # inside the kicked grid, whose far edges stop 1/16 pixel short
    check((min(xs), max(xs), min(ys), max(ys)) == (1792 * 16, 2304 * 16 - 1, 1936 * 16, 2160 * 16 - 1),
          f'{label}: grid spans X {min(xs)/16}..{max(xs)/16} Y {min(ys)/16}..{max(ys)/16}')
    st, xy = native.grid(asset, m_bits, zoom)
    for r, strip in enumerate(strips):
        for n, (s, t, x, y, z) in enumerate(strip):
            row, col = r + (n & 1), n >> 1
            k = row * GRID + col
            got = (st[2 * k], st[2 * k + 1], xy[2 * k], xy[2 * k + 1], 0)
            check((s, t, x, y, z) == got,
                  f'{label}: strip {r} vertex {n}: original {(hex(s), hex(t), x, y, z)} '
                  f'native {(hex(got[0]), hex(got[1]), got[2], got[3])}')
    return len(strips) * 2 * GRID


PROGRAM = {}


def sweep_case(args):
    seed, asset, template_dmem = args
    rng = random.Random(seed)
    yaw, pitch, roll = (rng.uniform(-math.pi, math.pi), rng.uniform(-1.5, 1.5),
                        rng.uniform(-0.3, 0.3))
    cy, sy, cp, sp, cr, sr = (math.cos(yaw), math.sin(yaw), math.cos(pitch),
                              math.sin(pitch), math.cos(roll), math.sin(roll))
    rot = [[cy * cr + sy * sp * sr, -cy * sr + sy * sp * cr, sy * cp],
           [cp * sr, cp * cr, -sp],
           [-sy * cr + cy * sp * sr, sy * sr + cy * sp * cr, cy * cp]]
    view = [0.0] * 16
    for j in range(3):
        for i in range(3):
            view[j * 4 + i] = rot[i][j]
    view[12:16] = [rng.uniform(-400, 400) for _ in range(3)] + [1.0]
    view = [flt(bits(v)) for v in view]
    zoom = flt(bits(rng.uniform(300.0, 1100.0)))
    m = SWEEP_NATIVE.matrix([bits(v) for v in view])
    dmem = dict(template_dmem)
    for r in range(4):
        dmem[0x200 + r] = struct.pack('<4I', *m[r * 4:r * 4 + 4])
    q = list(struct.unpack('<4I', dmem[0x204])); q[2] = bits(zoom)
    dmem[0x204] = struct.pack('<4I', *q)
    return compare_grid(SWEEP_NATIVE, asset, dmem, f'sweep {seed}')


SWEEP_NATIVE = None


# --- texels -------------------------------------------------------------------

def texels_from_freeze(decomp, gs_path, tex0):
    sys.path.insert(0, str(decomp / 'tools'))
    from extract_textures import psmt8_byte
    from gs_vram import read_localmem, read_clut_at, csm1_unswizzle_clut
    _b, lm = read_localmem(gs_path)
    tbp, tbw = tex0 & 0x3FFF, (tex0 >> 14) & 0x3F
    cbp = (tex0 >> 37) & 0x3FFF
    w, h = 1 << ((tex0 >> 26) & 15), 1 << ((tex0 >> 30) & 15)
    clut = csm1_unswizzle_clut(read_clut_at(lm, cbp))
    out = bytearray()
    for y in range(h):
        for x in range(w):
            i = lm[tbp * 256 + psmt8_byte(x, y, tbw // 2)]
            r, g, b, a = clut[i * 4:i * 4 + 4]
            out += bytes((r, g, b, min(255, a * 2)))
    return bytes(out)


def texels_from_disc(decomp, args, tex0):
    """The TEX0 texture after the decomp exporter replays the AREA11
    level-load GS upload from the user's disc."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('_export_level_bg',
                                                  decomp / 'tools/export_level.py')
    el = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(decomp / 'tools'))
    spec.loader.exec_module(el)
    iso = args.iso or (None if args.disc else decomp / 'Extermination-rebuilt.iso')
    check(args.disc is not None or iso.exists(),
          f'no disc: pass --iso or --disc ({iso} missing)')
    disc = el.BackgroundDisc(str(args.disc) if args.disc else None,
                             str(iso) if iso else None)
    lm, transfers, _sections = el.background_disc_localmem(disc, 0x0B, 0)
    return el.background_texels(lm, tex0)[2], len(transfers)


# --- screenshot metric ----------------------------------------------------------

def read_png(path):
    d = Path(path).read_bytes(); i, idat = 8, b''
    while i < len(d):
        n = struct.unpack('>I', d[i:i + 4])[0]; t = d[i + 4:i + 8]; c = d[i + 8:i + 8 + n]
        i += 12 + n
        if t == b'IHDR': w, h, _bd, ct = struct.unpack('>IIBB', c[:10])
        if t == b'IDAT': idat += c
    raw = zlib.decompress(idat); bpp = {2: 3, 6: 4}[ct]; s = w * bpp
    rows, prev, p = [], bytearray(s), 0
    for _ in range(h):
        f = raw[p]; p += 1; line = bytearray(raw[p:p + s]); p += s
        for x in range(s):
            a = line[x - bpp] if x >= bpp else 0; b = prev[x]; cc = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - cc), abs(a - cc), abs(a + b - 2 * cc)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else cc)) & 255
        rows.append(bytes(line)); prev = line
    return w, h, lambda x, y: tuple(rows[y][x * bpp:x * bpp + 3])


def read_bmp(path):
    d = Path(path).read_bytes()
    off, w, h, bpp = u32(d, 10), u32(d, 18), struct.unpack_from('<i', d, 22)[0], d[28]
    stride = (w * bpp // 8 + 3) & ~3; flip = h > 0; h = abs(h)
    def px(x, y):
        yy = h - 1 - y if flip else y
        o = off + yy * stride + x * bpp // 8
        return (d[o + 2], d[o + 1], d[o])
    return w, h, px


def region_metric(path, box):
    w, h, px = (read_png if str(path).endswith('.png') else read_bmp)(path)
    x0, y0, x1, y1 = (int(box[0] * w), int(box[1] * h), int(box[2] * w), int(box[3] * h))
    n = black = 0; tot = [0, 0, 0]
    for y in range(y0, y1, 2):
        for x in range(x0, x1, 2):
            c = px(x, y); n += 1
            black += c == (0, 0, 0)
            for k in range(3): tot[k] += c[k]
    return {'pixels': n, 'black': black, 'mean': [round(t / n, 1) for t in tot]}


def main():
    global PROGRAM, SWEEP_NATIVE
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--decomp-root', type=Path, default=ROOT.parent / 'Extermination')
    ap.add_argument('--asset', type=Path, default=ROOT / 'assets/scene_snow/background.embg')
    ap.add_argument('--scene', type=Path, default=ROOT / 'assets/scene_snow/scene.txt')
    ap.add_argument('--iso', type=Path, help='the user\'s disc image (default: '
                    '<decomp>/Extermination-rebuilt.iso)')
    ap.add_argument('--disc', type=Path, help='the user\'s disc directory (DATA/INDEX.IDX, '
                    'DATA/DATA.DAT) instead of --iso')
    ap.add_argument('--native', type=Path, help='native EM_CAPTURE BMP (optional metric)')
    ap.add_argument('--original', type=Path, help='original screenshot PNG (optional metric)')
    box = lambda v: tuple(float(x) for x in v.split(','))
    ap.add_argument('--native-box', type=box, default=(0.05, 0.0, 0.15, 0.1),
                    help='x0,y0,x1,y1 fractions: background in the native capture')
    ap.add_argument('--original-box', type=box, default=(0.03, 0.0, 0.15, 0.04),
                    help='x0,y0,x1,y1 fractions: background in the original PNG')
    args = ap.parse_args()
    decomp = args.decomp_root.resolve()
    elf = (decomp / 'config/SCUS_971.12').read_bytes()
    check(hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'not the pinned boot ELF')
    reference = decomp / 'build/startup-reference'
    check(args.asset.exists(), f'{args.asset} missing: run the decomp export_level.py --background')
    asset = args.asset.read_bytes()
    check(asset[:4] == b'EMBG' and u32(asset, 4) == 2, 'asset is not EMBG v2')
    tw, th = u32(asset, 8), u32(asset, 12)
    a_tex0, a_clamp, a_tex1 = u64(asset, 16), u64(asset, 24), u64(asset, 32)
    a_rgbaq, a_prim = u32(asset, 40), u32(asset, 44)
    a_test, a_zbuf = u64(asset, 88), u64(asset, 96)
    lines = [ln.split() for ln in args.scene.read_text().splitlines() if ln.startswith('background ')]
    check(lines == [['background', args.asset.name]], f'manifest background lines {lines}')

    # 3. ELF constants
    def elf_at(va, n): return elf[va - 0x100000 + 0x300:va - 0x100000 + 0x300 + n]
    consts = struct.unpack('<16I', elf_at(GRID_CONSTANTS, 0x40))
    want = [consts[i] for i in (0, 1, 4, 5, 8, 9, 10, 12, 13)]
    check(list(struct.unpack_from('<9I', asset, 48)) == want, 'asset grid constants != D_002535B0..EF')
    check(u32(asset, 84) == 1, 'asset texels are not the disc upload replay')
    template = elf_at(TEMPLATE, 16)
    check(a_prim == (u64(template, 0) >> 47) & 0x7FF and (u64(template, 0) >> 46) & 1,
          'asset PRIM != D_00253560 template')
    PROGRAM = kernel_program(elf)

    with tempfile.TemporaryDirectory(prefix='em-bg-') as tmp:
        native = Native(tmp)
        SWEEP_NATIVE = native
        why = native.lib.unsupported(asset, len(asset)).decode()
        check(why == '', f'backend refuses the asset: {why}')
        # a changed field must be refused, not approximated
        bad = bytearray(asset); struct.pack_into('<I', bad, 44, a_prim | 0x20)
        check(native.lib.unsupported(bytes(bad), len(bad)).decode() == 'PRIM FGE 1',
              'FGE not refused')
        for off, value, want in (
                (88, (a_test & ~(3 << 17)) | (3 << 17), 'TEST_1 not ZTE 1 / ZTST ALWAYS'),
                (88, (a_test & ~0xF) | 1 | (6 << 1), 'TEST_1 alpha test can drop pixels'),
                (88, a_test | (1 << 14), 'TEST_1 DATE 1'),
                (96, a_zbuf & ~(1 << 32), 'ZBUF_1 ZMSK 0')):
            bad = bytearray(asset); struct.pack_into('<Q', bad, off, value)
            got = native.lib.unsupported(bytes(bad), len(bad)).decode()
            check(got == want, f'{want} not refused (got {got!r})')
        bad = bytearray(asset); struct.pack_into('<I', bad, 84, 0)
        check(native.lib.unsupported(bytes(bad), len(bad)).decode() ==
              'texel source is not the disc upload replay', 'capture texels not refused')
        # 7. GS field-pixel footprint -> viewport fraction
        out = (C.c_float * 2)()
        for i in range(513):
            j = i % 225
            xy = (C.c_ushort * 2)((1792 + i) * 16, (1936 + j) * 16)
            native.lib.ndc(xy, out)
            want = (flt(bits(i / 512 * 2 - 1)), flt(bits(1 - j / 224 * 2)))
            check((out[0], out[1]) == want,
                  f'GS pixel edge ({i},{j}) -> NDC {out[0]},{out[1]}')

        clears = frames = verts = 0
        template_dmem = None
        for name in CAPTURES:
            path = reference / name
            check(path.exists(), f'capture {name} missing')
            ram = path.read_bytes()
            check(ram[0x810700:0x810702] == b'\x0b\x00', f'{name}: not AREA11/0')
            ctx = u32(ram, CTX_PTR) & 0x1FFFFFF
            pk = u32(ram, CTX_PACKETS) & 0x1FFFFFF
            check(u32(ram, ctx + 0x174) & 3 == 3, f'{name}: flags 0x20/0x21 not armed')
            # 1. both lists: draw env, Z-only clear, CALL channel 3
            for head in LIST_HEADS:
                (t0, q0, a0, _), (t1, q1, a1, _), (t2, q2, a2, _) = main_list_head(ram, head)
                check(t0 == 3 and q0 == 25, f'{name} {head:#x}: first tag is not the draw env')
                check(t1 == 3 and q1 == 8 and a1 == pk + 0x3A0,
                      f'{name} {head:#x}: second tag is not the 0x3A0 clear')
                # qword 0: VIF FLUSH + DIRECT 7; qword 1: GIF tag, 6 A+D
                clear = ram[a1:a1 + 16 * 8]
                check(u32(clear, 12) >> 24 == 0x50 and u32(clear, 12) & 0xFFFF == 7,
                      f'{name}: clear packet is not DIRECT 7')
                tag = u64(clear, 16)
                check(tag & 0x7FFF == 6 and (u64(clear, 24) & 0xF) == 0xE, 'clear GIF tag')
                ad = [(u64(clear, 16 * k + 8) & 0xFF, u64(clear, 16 * k)) for k in range(2, 8)]
                check([r for r, _ in ad] == [0x47, 0x00, 0x01, 0x05, 0x05, 0x47],
                      f'{name}: clear registers {[hex(r) for r, _ in ad]}')
                test, prim = ad[0][1], ad[1][1]
                check(test & 1 == 1 and (test >> 1) & 7 == 0 and (test >> 12) & 3 == 2,
                      f'{name}: clear TEST {test:#x} is not ATE/NEVER/ZB_ONLY')
                check(prim == 6, f'{name}: clear PRIM {prim:#x} is not a sprite')
                check(ad[3][1] >> 32 == 0 and ad[4][1] >> 32 == 0, f'{name}: clear Z is not 0')
                xy = [ad[3][1], ad[4][1]]
                check(xy[0] & 0xFFFF == 0x7000 and xy[1] & 0xFFFF == 0x9000 and
                      (xy[0] >> 16) & 0xFFFF == 0x7900 and (xy[1] >> 16) & 0xFFFF == 0x8700,
                      f'{name}: clear sprite does not cover the field')
                check(t2 == 5, f'{name} {head:#x}: third tag is not a CALL')
                clears += 1
            node = u32(ram, ctx + 0x1D8)
            slot = u32(ram, ctx + 0x9C)
            check(node in (a2, 0x5635C0 + slot * 0x100000), f'{name}: node {node:#x}')
            # 2. channel 3's list state at its MSCAL
            calls, kicks = replay(ram, node)
            check(calls == [KERNEL_PACKET] and len(kicks) == 1 and kicks[0][0] == 0,
                  f'{name}: channel 3 calls {calls} kicks {len(kicks)}')
            _, regs, dmem = kicks[0]
            tex0 = u64(ram, ctx + 0x1D0)
            rgbaq = 0
            for lane, value in enumerate(struct.unpack_from('<4f', ram, ctx + 0x1C0)):
                rgbaq |= (int(128.0 * value) & 0xFF) << (8 * lane)
            check(regs.get(0x06) == tex0 == a_tex0, f'{name}: TEX0')
            check(regs.get(0x08) == a_clamp and regs.get(0x14) == a_tex1, f'{name}: CLAMP/TEX1')
            check(regs.get(0x01, 0) & 0xFFFFFFFF == rgbaq == a_rgbaq, f'{name}: RGBAQ')
            check(flt(regs[0x01] >> 32) == 1.0, f'{name}: RGBAQ Q')
            test, zbuf = regs[0x47], regs[0x4E]
            check(test == a_test and zbuf == a_zbuf, f'{name}: TEST_1/ZBUF_1 != asset')
            check((test >> 16) & 1 and (test >> 17) & 3 == 1, f'{name}: TEST {test:#x} not ZTE/ALWAYS')
            check(test & 1 == 0 or (test >> 1) & 7 == 1, f'{name}: TEST {test:#x} alpha test drops pixels')
            check((zbuf >> 32) & 1 == 1, f'{name}: ZBUF {zbuf:#x} writes Z')
            check(regs.get(0x42) is not None, f'{name}: no ALPHA_1 (ABE 0 anyway)')
            for base in (0x000, 0x081, 0x102):
                check(dmem.get(base) == template, f'{name}: dmem {base:#x} != D_00253560')
            up = b''.join(dmem[0x204 + k] for k in range(4))
            check(up[:8] + up[12:] == elf_at(GRID_CONSTANTS, 0x40)[:8] + elf_at(GRID_CONSTANTS, 0x40)[12:],
                  f'{name}: uploaded grid constants')
            check(up[8:12] == ram[ctx + 0x2468:ctx + 0x246C], f'{name}: zoom != ctx+0x2468')
            # 4. matrix
            view_bits = list(struct.unpack_from('<16I', ram, ctx + 0x2380))
            m = native.matrix(view_bits)
            uploaded = [w for a in range(0x200, 0x204) for w in struct.unpack('<4I', dmem[a])]
            check(m == uploaded, f'{name}: native matrix != uploaded D_00253570')
            check(list(struct.unpack_from('<16I', ram, 0x253570)) == uploaded, f'{name}: D_00253570')
            # 5. original kernel vs native
            verts += compare_grid(native, asset, dmem, name)
            template_dmem = dmem
            frames += 1

        hub = reference / HUB
        hub_calls = 0
        if hub.exists():
            ram = hub.read_bytes()
            for head in LIST_HEADS:
                hub_calls += sum(1 for t, _q, _a, _d in main_list_head(ram, head) if t == 5)
            check(hub_calls == 0, 'status hub list CALLs the background')

        seeds = list(range(64)) if FULL else [1, 2]
        from reference_mode import parallel_map
        swept = sum(parallel_map(sweep_case, [(s, asset, template_dmem) for s in seeds]))

    # 6. texels: disc replay == asset == every GS freeze
    check(len(asset) == 104 + tw * th * 4, 'asset size')
    disc_texels, transfers = texels_from_disc(decomp, args, a_tex0)
    check(disc_texels == asset[104:], 'disc upload replay texels differ from the asset')
    freezes = 0
    for name in GS_FREEZES:
        path = reference / name
        if not path.exists(): continue
        check(texels_from_freeze(decomp, path, a_tex0) == disc_texels,
              f'{name}: GS freeze texels differ from the disc replay')
        freezes += 1
    check(freezes, 'no GS freeze to check the texels')

    report = {'status': 'PASS', 'tex0': hex(a_tex0), 'texture': f'{tw}x{th}',
              'clamp1': hex(a_clamp), 'tex1': hex(a_tex1), 'rgbaq': hex(a_rgbaq),
              'prim': hex(a_prim), 'captures': frames, 'z_only_clears': clears,
              'kernel_vertices': verts, 'sweep_vertices': swept,
              'disc_transfers': transfers,
              'gs_freezes': freezes, 'hub_background_calls': hub_calls}
    # The two cameras differ (native first control vs the original
    # screenshot), so each image gets its own background box.
    if args.original:
        report['original_region'] = region_metric(args.original, args.original_box)
    if args.native:
        report['native_region'] = region_metric(args.native, args.native_box)
        report['native_frame'] = region_metric(args.native, (0.0, 0.0, 1.0, 1.0))
    banner(f'{frames} captures', part(len(seeds), 64, 'sweep views'))
    print(json.dumps(report))


if __name__ == '__main__':
    main()
