#!/usr/bin/env python3
"""AREA11 record 12, the director 0x008253F0: native vs original.

Every part runs in every mode (quick mode samples the bulk inputs;
EM_TEST_FULL=1 runs the exhaustive sweep, about 45 s):

0. SDK atan2f. The original 0011DBB8 (atanf), 0011C4C8 (atan2f kernel) and
   0011E620 (wrapper) against their translations: every branch boundary of
   atanf, signed zeros, x = 1, huge/tiny ratios, random words. Bit-identical.
1. Owner ticks. The original owner (the user's AREA11 overlay at its arena
   0x823500) executes with the main-ELF 001BA1C0, 001C4760, 001B1EA0, 0011E620
   and the SDK kernel; 001BA1A0/001BA1F0/001AFC10 are recorded (001BA1F0
   returns 0, 1 or 3). Inputs: node +0/+4/+5, D_00810813, D_00810793 (flag
   0x3B), player positions inside/outside each quad and heights at every gate
   edge (260/275/280/285 and their float neighbours, NaN, infinities, signed
   zero). Compared with em_director_original_tick: the ordered worker calls
   with their arguments, the 001B1EA0 call arguments, every modelled byte
   afterwards, that the original writes nothing else (except 0011E620's errno
   on a zero vector, which is not modelled), and the kernel on every argument
   pair the original produced.
2. Polygon. The original 001B1EA0 against em_director_original_001B1EA0 over
   the three quads and Roger's 0x82AB80: vertices, points on and 1e-3/0.05/1
   off every edge, the edge lines beyond the vertices, points inside the
   bounding box of the irregular quad 0x82AC20 but outside it, float
   neighbours of the bounding box, random points; counts -1..3; modes 1, 2,
   3, -1, 0x7FFFFFFF; non-finite points (native refuses).
3. Fail-stop. Each missing or failing worker/storage on a reached path faults
   at its address.
4. Route. The native module replayed over every frame of the original
   pad-only route traces (../Extermination/build/s87/route, 15 beats,
   FIRST_LEVEL_ROUTE.md): the director node's +0/+4/+5, D_00810813 and every
   script start (frame, block and entry) against the next captured row, and
   001C4760's effect against the route snapshots (D_00810CC4 0 -> 1 across
   beat 10, unchanged afterwards).

Arithmetic model of the oracle and the translation: EE add/sub with the
single guard bit, truncating mul, nearest div (em_pose_math.h, the model the
truck, sine and pose captures settled). MSUB.S is ACC - fs*ft: that is what
mwcc emits for `b*t - a*inverse` in quat_nlerp (Extermination
src/quat_nlerp.c, 67 of 69 instructions identical to the original) and what
em_pose_transition's pose_msub uses. Only addresses and values are printed;
no original bytes are embedded.
"""
import ctypes as C
import hashlib
import itertools
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys

from test_pickup_owner_reference import OwnerOracle
from test_item_sdk_math_reference import Original as SdkOracle
from test_pose_transition_reference import add, rounded
from test_point_light_reference import bits, number, STACK
import reference_mode as rm

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
ROUTE = DECOMP / 'build/s87/route'
ARENA, SIZE = 0x823500, 0x7800
ENTRY = 0x8253F0
NODE = 0x7A93F0                 # record 12's node in the route captures
QUADS = (0x82ABE0, 0x82AC20, 0x82AC60)
SCRIPTS = (0x8294C0, 0x829A40, 0x829CC0)
START, POLL, QUAD, FREE = 0x1BA1A0, 0x1BA1F0, 0x1B1EA0, 0x1AFC10
KERNEL = 0x11C4C8
D813, D793, PLAYER, CC3, B0, B1 = 0x810813, 0x810793, 0x810350, 0x810CC3, 0x8106B0, 0x8106B1
MODELLED = {NODE, NODE + 4, NODE + 5, D813, CC3, CC3 + 1, B0, B1}
MASK64 = (1 << 64) - 1


class DirectorOracle(OwnerOracle, SdkOracle):
    """The pickup-owner interpreter with the pose oracle's scalar FPU model."""
    def plain(self, w):
        op, rs, rt, rd, fd, fn = w >> 26, w >> 21 & 31, w >> 16 & 31, w >> 11 & 31, w >> 6 & 31, w & 63
        if op == 17 and rs == 16 and fn in (0, 1, 2, 24, 25, 26, 28, 29, 30, 31):
            a, b = number(self.f[rd]), number(self.f[rt])
            if fn == 0: self.f[fd] = bits(add(a, b))
            elif fn == 1: self.f[fd] = bits(add(a, -b))
            elif fn == 2: self.f[fd] = bits(rounded(a * b))
            elif fn == 24: self.sacc = add(a, b)
            elif fn == 25: self.sacc = add(a, -b)
            elif fn == 26: self.sacc = rounded(a * b)
            elif fn == 28: self.f[fd] = bits(add(self.sacc, rounded(a * b)))
            elif fn == 29: self.f[fd] = bits(add(self.sacc, -rounded(a * b)))  # ACC - fs*ft
            elif fn == 30: self.sacc = add(self.sacc, rounded(a * b))
            else: self.sacc = add(self.sacc, -rounded(a * b))
            self.r[0] = 0
        else:
            super().plain(w)


def kernel_call(o):
    """Run the original 0011C4C8 in a separate oracle and log (y, x, result)."""
    y, x = o.f[12], o.f[13]
    k = DirectorOracle(ELF)
    k.run(KERNEL, floats=(number(y), number(x)))
    o.f[0] = k.f[0]
    o.klog.append((y, x, k.f[0]))


def new_oracle():
    o = DirectorOracle(ELF)
    o.write(ARENA, OVERLAY)
    o.klog, o.events = [], []
    o.calls[KERNEL] = kernel_call
    o.calls[START] = lambda o: o.events.append(('start', o.r[4] & 0xFFFFFFFF, o.r[5] & 0xFFFFFFFF))
    o.calls[FREE] = lambda o: o.events.append(('free', o.r[4] & 0xFFFFFFFF))
    return o


SHIM = r'''
#include <string.h>
#include "game/em_director_original.h"
static struct {
    int32_t *ev; int nev, cap; int poll; unsigned fail;
} S;
static void event(int32_t k, int32_t a, int32_t b)
{
    if (S.nev < S.cap) { int32_t *e = S.ev + 3 * S.nev++; e[0] = k; e[1] = a; e[2] = b; }
}
static int w_start(void *ctx, uint32_t block, uint32_t entry)
{ (void)ctx; event(1, (int32_t)block, (int32_t)entry); return S.fail & 1 ? -1 : 0; }
static int w_poll(void *ctx, uint32_t self, int32_t *r)
{ (void)ctx; event(2, (int32_t)self, 0); *r = S.poll; return S.fail & 2 ? -1 : 0; }
static int w_free(void *ctx, uint32_t self)
{ (void)ctx; event(4, (int32_t)self, 0); return S.fail & 8 ? -1 : 0; }

/* drop: 1 start, 2 poll, 8 free, 16 d813, 32 d793, 64 player, 128 cc3,
 * 256 quads, 512 b05, 1024 atan tables */
int shim_tick(uint8_t node[3], uint32_t self, uint8_t *d813, uint8_t d793, const float player[3],
              uint8_t cc3[2], uint8_t b0b1[2], const float *quads, const EmDirectorAtanTables *tables,
              int poll, unsigned drop, unsigned fail, int32_t *events, int *nevents, uint32_t *fault)
{
    S.ev = events; S.nev = 0; S.cap = 16; S.poll = poll; S.fail = fail;
    EmDirectorOriginalNode n = {&node[0], &node[1], drop & 512 ? 0 : &node[2], self};
    EmDirectorOriginalWorld w;
    memset(&w, 0, sizeof w);
    w.d810813 = drop & 16 ? 0 : d813;
    w.d810793 = drop & 32 ? 0 : &d793;
    w.d810350 = drop & 64 ? 0 : player;
    w.d810CC3 = drop & 128 ? 0 : cc3;
    w.d8106B0 = &b0b1[0]; w.d8106B1 = &b0b1[1];
    for (int q = 0; q < 3; ++q)
        w.quad[q] = drop & 256 ? 0 : (const float (*)[4])(quads + 16 * q);
    w.d26C5D8 = drop & 1024 ? 0 : tables;
    EmDirectorOriginalWorkers k = {0, drop & 1 ? 0 : w_start, drop & 2 ? 0 : w_poll,
                                   drop & 8 ? 0 : w_free};
    *fault = 0;
    int rc = em_director_original_tick(&n, &w, &k, fault);
    *nevents = S.nev;
    return rc;
}

int shim_1B1EA0(int32_t mode, const float *point, const float *poly, int32_t count,
                const EmDirectorAtanTables *tables, int32_t *result)
{
    return em_director_original_001B1EA0(mode, point, (const float (*)[4])poly, count, tables, result);
}

int shim_atan(int which, const EmDirectorAtanTables *t, float y, float x, float *out)
{
    return which == 0 ? em_director_original_0011DBB8(t, y, out)
         : which == 1 ? em_director_original_0011C4C8(t, y, x, out)
                      : em_director_original_0011E620(t, y, x, out);
}

int shim_quads(const uint8_t *overlay, size_t size, float *out)
{
    return em_director_original_load_quads(overlay, size, (float (*)[4][4])out);
}

int shim_tables(const uint8_t *elf, size_t size, EmDirectorAtanTables *out)
{
    return em_director_original_load_atan_tables(elf, size, out);
}
'''


class Tables(C.Structure):
    _fields_ = [('hi', C.c_float * 4), ('lo', C.c_float * 4), ('aT', C.c_float * 11)]


def build_native():
    out = ROOT / 'build/director_original_reference'
    out.mkdir(parents=True, exist_ok=True)
    source = out / 'shim.c'
    source.write_text(SHIM)
    lib = out / ('shim.dylib' if sys.platform == 'darwin' else 'shim.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    '-ffp-contract=off', '-Isrc', str(source), 'src/game/em_director_original.c',
                    '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    F = C.POINTER(C.c_float)
    U8 = C.POINTER(C.c_uint8)
    native.shim_tick.argtypes = [U8, C.c_uint32, U8, C.c_uint8, F, U8, U8, F, C.POINTER(Tables), C.c_int,
                                 C.c_uint, C.c_uint, C.POINTER(C.c_int32), C.POINTER(C.c_int),
                                 C.POINTER(C.c_uint32)]
    native.shim_1B1EA0.argtypes = [C.c_int32, F, F, C.c_int32, C.POINTER(Tables), C.POINTER(C.c_int32)]
    native.shim_atan.argtypes = [C.c_int, C.POINTER(Tables), C.c_float, C.c_float, F]
    native.shim_quads.argtypes = [C.c_char_p, C.c_size_t, F]
    native.shim_tables.argtypes = [C.c_char_p, C.c_size_t, C.POINTER(Tables)]
    return native


# Module globals set in main() before any fork (reference_mode.parallel_map).
ELF = OVERLAY = NATIVE = TABLES = QUAD_FLOATS = None


def f32(value):
    return number(bits(value))


def neighbours(value):
    b = bits(value)
    return [number(b - 1) if value > 0 else number(b + 1), value, number(b + 1) if value > 0 else number(b - 1)]


def native_tick(node, d813, d793, player, cc3, b0b1, poll, drop=0, fail=0):
    n = (C.c_uint8 * 3)(*node)
    s813 = C.c_uint8(d813)
    c = (C.c_uint8 * 2)(*cc3)
    bb = (C.c_uint8 * 2)(*b0b1)
    events = (C.c_int32 * 48)()
    nev, fault = C.c_int(), C.c_uint32()
    rc = NATIVE.shim_tick(n, NODE, C.byref(s813), d793, (C.c_float * 3)(*player), c, bb, QUAD_FLOATS,
                          C.byref(TABLES), poll, drop, fail, events, C.byref(nev), C.byref(fault))
    evs = []
    for i in range(nev.value):
        k, a, b = (events[3 * i + j] & 0xFFFFFFFF for j in range(3))
        evs.append({1: ('start', a, b), 2: ('poll', a), 4: ('free', a)}[k])
    return rc, fault.value, list(n), s813.value, list(c), list(bb), evs


def original_tick(node, d813, d793, player, cc3, b0b1, poll):
    """Execute the owner; 001BA1C0, 001C4760, 001B1EA0, 0011E620 and the SDK
    kernel run as original instructions; 001BA1A0/001BA1F0/001AFC10 are
    recorded (001BA1F0 returns `poll`)."""
    o = new_oracle()
    for off, value in zip((0, 4, 5), node):
        o.save(NODE + off, value, 1)
    o.save(D813, d813, 1)
    o.save(D793, d793, 1)
    o.write(PLAYER, struct.pack('<4f', *player, 1.0))
    o.save(CC3, cc3[0], 1)
    o.save(CC3 + 1, cc3[1], 1)
    o.save(B0, b0b1[0], 1)
    o.save(B1, b0b1[1], 1)

    def poll_call(o):
        o.events.append(('poll', o.r[4] & 0xFFFFFFFF))
        o.r[2] = poll
    o.calls[POLL] = poll_call
    quad_calls = []
    original_load = o.load

    def load(address, size=4):
        # The fetch of 001B1EA0's first instruction (once per call; the
        # function never branches back to it) records its arguments.
        if address == QUAD and size == 4:
            quad_calls.append(tuple(o.r[i] & 0xFFFFFFFF for i in (4, 5, 6, 7)))
        return original_load(address, size)
    o.load = load
    before = dict(o.mem)
    o.run(ENTRY, (NODE,))
    o.load = original_load
    written = {a for a, v in o.mem.items() if before.get(a, 0) != v and not (STACK - 0x1000 <= a < STACK)}
    return o, written, quad_calls


def expected_quad(node, d813, player):
    """The 001B1EA0 call the owner makes, if any (to check the recorded
    arguments independently of the native module)."""
    if node[1] != 1 or node[2] != 0:
        return []
    beat = {0: 0, 1: 0, 0x10: 1, 0x11: 1, 0x20: 2}.get(d813)
    if beat is None:
        return []
    y = player[1]
    if y < (260.0, 275.0, 285.0)[beat] or (beat == 0 and not y <= 280.0):
        return []
    return [(0, PLAYER, QUADS[beat], 4)]


def compare_tick(label, node, d813, d793, player, cc3, b0b1, poll):
    o, written, quad_calls = original_tick(node, d813, d793, player, cc3, b0b1, poll)
    rc, fault, n, s813, c, bb, evs = native_tick(node, d813, d793, player, cc3, b0b1, poll)
    assert quad_calls == expected_quad(node, d813, player), (label, '001B1EA0 calls', quad_calls)
    if quad_calls and not all(math.isfinite(v) for v in (player[0], player[2])):
        assert rc < 0 and fault == QUAD, (label, 'non-finite X/Z reached 001B1EA0; native must fault')
        return 'domain-fault'
    assert rc >= 0, (label, 'native faulted', hex(fault))
    assert evs == o.events, (label, 'worker calls', evs, o.events)
    # 0011E620's zero-vector branch writes errno (unmodelled); every other
    # original write must be a modelled byte.
    zero_vector = any(y & 0x7FFFFFFF == 0 and x & 0x7FFFFFFF == 0 for y, x, _ in o.klog)
    stray = written - MODELLED
    assert not stray or zero_vector, (label, 'original wrote outside the modelled bytes', sorted(map(hex, stray)))
    for y, x, r in o.klog:
        out = C.c_float()
        assert NATIVE.shim_atan(1, C.byref(TABLES), number(y), number(x), C.byref(out)) == 0 and \
            bits(out.value) == r, (label, 'kernel on the original arguments', hex(y), hex(x), hex(r))
    expect = ([o.load(NODE, 1), o.load(NODE + 4, 1), o.load(NODE + 5, 1)], o.load(D813, 1),
              [o.load(CC3, 1), o.load(CC3 + 1, 1)], [o.load(B0, 1), o.load(B1, 1)])
    assert (n, s813, c, bb) == expect, (label, 'state', (n, s813, c, bb), expect)
    freed = any(e[0] == 'free' for e in o.events)
    assert rc == (0 if freed else 1), (label, 'result', rc, freed)
    return 'start' if any(e[0] == 'start' for e in o.events) else 'free' if freed else \
        'complete' if s813 != d813 else 'arm' if n != list(node) else \
        'gate-miss' if quad_calls else 'none'


def protocol_case(case):
    return compare_tick(('tick',) + tuple(case), *case)


def geometry_case(case):
    quad_index, point, count, mode = case
    address = QUADS[quad_index] if quad_index < 3 else 0x82AB80
    o = new_oracle()
    o.write(0x950000, struct.pack('<4f', *point, 1.0))
    o.run(QUAD, (mode & MASK64, 0x950000, address, count & MASK64))   # EE registers sign-extend
    poly = (C.c_float * 16)(*struct.unpack_from('<16f', OVERLAY, address - ARENA))
    result = C.c_int32()
    rc = NATIVE.shim_1B1EA0(mode, (C.c_float * 3)(*point), poly, count, C.byref(TABLES), C.byref(result))
    label = ('geometry', hex(address), point, count, mode)
    if mode in (1, 2) and count >= 3:
        assert rc < 0, (label, 'modes 1/2 are not translated; native must refuse')
        return 'refused'
    if count >= 3 and mode == 0 and not all(math.isfinite(v) for v in (point[0], point[2])):
        assert rc < 0, (label, 'non-finite input must be refused')
        return 'refused'
    assert rc == 0, (label, 'native refused')
    for y, x, r in o.klog:
        out = C.c_float()
        assert NATIVE.shim_atan(1, C.byref(TABLES), number(y), number(x), C.byref(out)) == 0 and \
            bits(out.value) == r, (label, 'kernel on the original arguments', hex(y), hex(x), hex(r))
    assert result.value == (o.r[2] & 0xFFFFFFFF), (label, 'result', result.value, o.r[2])
    return result.value


def atan_case(case):
    """(which, y, x): 0 = 0011DBB8(y), 1 = 0011C4C8(y, x), 2 = 0011E620(y, x)."""
    which, y, x = case
    o = DirectorOracle(ELF)
    o.run((0x11DBB8, 0x11C4C8, 0x11E620)[which], floats=(y, x))
    out = C.c_float()
    rc = NATIVE.shim_atan(which, C.byref(TABLES), y, x, C.byref(out))
    assert rc == 0 and bits(out.value) == o.f[0], (('atan', which, y, x), hex(bits(out.value)), hex(o.f[0]))
    return 0


def atan_arguments(rng, count):
    """Every branch of 0011C4C8/0011DBB8 on finite arguments, plus a sample."""
    cases = []
    ranges = [0x00000001, 0x30FFFFFF, 0x31000000, 0x3EDFFFFF, 0x3EE00000, 0x3F2FFFFF, 0x3F300000,
              0x3F97FFFF, 0x3F980000, 0x401BFFFF, 0x401C0000, 0x507FFFFF, 0x50800000, 0x7F7FFFFF]
    for word in ranges:
        for sign in (0, 0x80000000):
            cases.append((0, number(word | sign), 0.0))
    for _ in range(count):
        cases.append((0, number(rng.randrange(0, 0x7F800000) | rng.choice((0, 0x80000000))), 0.0))
    special = [0.0, -0.0, 1.0, -1.0, 2.0, 0.5, 1e-30, -1e-30, 1e30, -1e30, 3.0e38, 123.25, -77.5]
    for y, x in itertools.product(special, special):
        cases.append((1, y, x))
        cases.append((2, y, x))
    for _ in range(count):
        y = number(rng.randrange(0, 0x7F800000) | rng.choice((0, 0x80000000)))
        x = number(rng.randrange(0, 0x7F800000) | rng.choice((0, 0x80000000)))
        cases.append((1, y, x))
        cases.append((1, rng.uniform(-3000, 3000), rng.uniform(-3000, 3000)))
    return [(w, f32(y), f32(x)) for w, y, x in cases]


def quad_points(address, rng, extra):
    v = struct.unpack_from('<16f', OVERLAY, address - ARENA)
    corners = [(v[i * 4], v[i * 4 + 2]) for i in range(4)]
    xs, zs = [c[0] for c in corners], [c[1] for c in corners]
    pts = list(corners)
    for i in range(4):
        (ax, az), (bx, bz) = corners[i], corners[(i + 1) & 3]
        length = math.hypot(bx - ax, bz - az)
        nx, nz = -(bz - az) / length, (bx - ax) / length
        for t in (0.001, 0.25, 0.5, 0.75, 0.999):
            px, pz = f32(ax + t * (bx - ax)), f32(az + t * (bz - az))
            pts.append((px, pz))
            for d in (1e-3, 0.05, 1.0):
                pts.append((f32(px + d * nx), f32(pz + d * nz)))
                pts.append((f32(px - d * nx), f32(pz - d * nz)))
        # the edge's line beyond both vertices
        pts.append((f32(ax - 0.5 * (bx - ax)), f32(az - 0.5 * (bz - az))))
        pts.append((f32(bx + 0.5 * (bx - ax)), f32(bz + 0.5 * (bz - az))))
    cx, cz = sum(xs) / 4, sum(zs) / 4
    pts += [(f32(cx), f32(cz)), (min(xs), min(zs)), (min(xs), max(zs)), (max(xs), min(zs)), (max(xs), max(zs))]
    for x in neighbours(f32(min(xs))) + neighbours(f32(max(xs))):
        pts.append((x, f32(cz)))
    for z in neighbours(f32(min(zs))) + neighbours(f32(max(zs))):
        pts.append((f32(cx), z))
    for _ in range(extra):
        pts.append((f32(rng.uniform(min(xs) - 10, max(xs) + 10)), f32(rng.uniform(min(zs) - 10, max(zs) + 10))))
    return pts


def route_replay():
    """Replay every captured frame. Row f+1 is the state after frame f (rows
    are sampled at the main-loop top 0x1AAF28); the director (#21) runs in the
    pool walk after the player stage and after Roger (#17)."""
    totals = dict(frames=0, starts=[], completions=[], polls=0, gate_tests=0, cc4=0)
    for trace_path in sorted(ROUTE.glob('*/trace.json')):
        beat = trace_path.parent.name
        rows = json.loads(trace_path.read_text())['rows']
        for f in range(len(rows) - 1):
            cur, nxt = rows[f], rows[f + 1]
            h, hn = bytes.fromhex(cur['director_r12']['h']), bytes.fromhex(nxt['director_r12']['h'])
            s, sn = bytes.fromhex(cur['director_r12']['s1F0']), bytes.fromhex(nxt['director_r12']['s1F0'])
            assert h[2] == 9 and hn[2] == 9, (beat, f, 'node is not the class-9 director')
            d813 = bytes.fromhex(cur['d2'])[0x3B]
            n813 = bytes.fromhex(nxt['d2'])[0x3B]
            # Roger r8 (0x8237E0) runs earlier in the same walk and writes
            # D_00810813 = 1 (script 0x828990's op06) and 0x11 (0x823A04).
            if n813 != d813 and n813 in (0x01, 0x11):
                d813 = n813
            d793 = bytes.fromhex(cur['story790'])[3]
            player = tuple(f32(v) for v in nxt['pos'])
            # 001BA1F0 is the script step; the block leaves the active state
            # (+0x1F0 byte 1) in the call that returns nonzero.
            poll = 1 if (s[0] == 1 and sn[0] != 1) else 0
            rc, fault, n, s813, c, bb, evs = native_tick((h[0], h[4], h[5]), d813, d793, player,
                                                         (0, totals['cc4']), (0, 0), poll)
            label = (beat, f, cur['counter'])
            assert rc == 1, (label, 'native result', rc, hex(fault))
            assert n == [hn[0], hn[4], hn[5]], (label, 'node +0/+4/+5', n, [hn[0], hn[4], hn[5]])
            assert s813 == n813, (label, 'D_00810813', hex(s813), hex(n813))
            starts = [e for e in evs if e[0] == 'start']
            captured_start = sn[0] == 1 and s[0] != 1
            assert bool(starts) == captured_start, (label, 'script start', starts, captured_start)
            if starts:
                entry = struct.unpack_from('<I', sn, 8)[0]
                assert starts[0] == ('start', NODE + 0x1F0, entry), (label, 'start', starts[0], hex(entry))
                totals['starts'].append((beat, f + 1, hex(entry)))
            if s813 != d813:
                totals['completions'].append((beat, f + 1, hex(s813)))
            totals['cc4'] = c[1]
            totals['polls'] += sum(e[0] == 'poll' for e in evs)
            totals['gate_tests'] += bool(expected_quad((h[0], h[4], h[5]), d813, player))
            totals['frames'] += 1
    # 001C4760(1, 1): D_00810CC3[1] across beat 10 (source snapshot: beat 08).
    before = (ROUTE / '08_truck_crossing/eeMemory.bin').read_bytes()[CC3:CC3 + 2]
    after = (ROUTE / '10_cage_roof_roger/eeMemory.bin').read_bytes()[CC3:CC3 + 2]
    later = [(ROUTE / b / 'eeMemory.bin').read_bytes()[CC3:CC3 + 2]
             for b in ('11_crevice_prompt', '13_east_tower', '14_roger_encounter')]
    assert before[1] == 0 and after[1] == 1 and totals['cc4'] == 1 and all(x == after for x in later), \
        ('D_00810CC4 across the route', before.hex(), after.hex(), [x.hex() for x in later])
    assert before[0] == after[0], 'D_00810CC3[0] changed across beat 10'
    return totals


def fault_cases():
    """Each worker/pointer on the reached path faults at its address when
    NULL (drop) or failing (fail): (label, node, d813, poll, drop, fail, address)."""
    return [
        ('free NULL', (1, 3, 0), 0, 0, 8, 0, FREE),
        ('free fails', (1, 2, 0), 0, 0, 0, 8, FREE),
        ('flag NULL', (0, 0, 0), 0, 0, 32, 0, 0x1BA1C0),
        ('step NULL', (1, 1, 0), 0, 0, 16, 0, 0x825468),
        ('player NULL', (1, 1, 0), 0, 0, 64, 0, 0x825538),
        ('quad data NULL', (1, 1, 0), 0, 0, 256, 0, QUADS[0]),
        ('atan tables NULL', (1, 1, 0), 0, 0, 1024, 0, QUAD),
        ('start NULL', (1, 1, 0), 0, 0, 1, 0, START),
        ('start fails', (1, 1, 0), 0, 0, 0, 1, START),
        ('poll NULL', (1, 1, 1), 0, 1, 2, 0, POLL),
        ('poll fails', (1, 1, 1), 0x10, 1, 0, 2, POLL),
        ('001C4760 array NULL', (1, 1, 1), 0, 1, 128, 0, 0x1C4760),
        ('sub-state NULL', (1, 1, 1), 0x20, 1, 512, 0, 0x8256D0),
    ]


def main():
    global ELF, OVERLAY, NATIVE, TABLES, QUAD_FLOATS
    ELF = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(ELF).hexdigest() == ELF_SHA256
    OVERLAY = (DECOMP / 'extract/OVERLAY/AREA11.BIN').read_bytes()
    assert len(OVERLAY) == SIZE and OVERLAY[:4] == b'MWo3' and struct.unpack_from('<I', OVERLAY, 8)[0] == ARENA, \
        'not the original AREA11 overlay'
    ram = (ROUTE / '10_cage_roof_roger/eeMemory.bin').read_bytes()
    for lo, hi in ((0x8253F0, 0x8254FC), (0x825500, 0x825760), (0x82ABE0, 0x82ACA0)):
        assert ram[lo:hi] == OVERLAY[lo - ARENA:hi - ARENA], ('the route capture does not hold the overlay at', hex(lo))
    assert struct.unpack_from('<i', ELF, 0x26C5D0 - 0x100000 + 0x300)[0] == 1 and \
        struct.unpack_from('<i', ram, 0x26C5D0)[0] == 1, 'D_0026C5D0 (0011E620 mode) is not 1'
    NATIVE = build_native()
    TABLES = Tables()
    assert NATIVE.shim_tables(ELF, len(ELF), C.byref(TABLES)) == 0
    assert bytes(TABLES) == ELF[0x26C5D8 - 0x100000 + 0x300:][:76], 'table loader'
    assert NATIVE.shim_tables(ELF[:-1], len(ELF) - 1, C.byref(Tables())) < 0
    QUAD_FLOATS = (C.c_float * 48)()
    assert NATIVE.shim_quads(OVERLAY, len(OVERLAY), QUAD_FLOATS) == 0
    for q, address in enumerate(QUADS):
        assert [bits(QUAD_FLOATS[16 * q + i]) for i in range(16)] == \
            list(struct.unpack_from('<16I', OVERLAY, address - ARENA)), 'quad loader'
    assert NATIVE.shim_quads(OVERLAY[:-1], len(OVERLAY) - 1, QUAD_FLOATS) < 0
    assert NATIVE.shim_quads(b'XXXX' + OVERLAY[4:], len(OVERLAY), QUAD_FLOATS) < 0
    assert NATIVE.shim_quads(OVERLAY, len(OVERLAY), QUAD_FLOATS) == 0

    # 0. The SDK atan2f path, standalone.
    rng = random.Random(0x11C4C8)
    atans = atan_arguments(rng, rm.pick(6000, 120))
    atan_total = len(atans) + 3 * (6000 - rm.pick(6000, 120))   # three random draws per sample
    rm.parallel_map(atan_case, atans)

    # 1. Owner ticks: state switch, dispatch, sub-states, polls, gate edges.
    inside_xz = ((360.0, 240.0), (476.0, 284.0), (425.0, 190.0))   # inside quad 0 / 1 / 2
    outside_xz = ((390.0, 240.0), (455.0, 291.0), (425.0, 170.0))  # outside (1: in the bbox only)
    heights = sorted({h for bound in (260.0, 275.0, 280.0, 285.0) for h in neighbours(bound)} |
                     {0.0, -0.0, 270.0, 290.0, 1e30, -1e30}, key=lambda v: (v, bits(v)))
    heights += [math.inf, -math.inf, math.nan]
    states = range(256) if rm.FULL else (0, 1, 2, 3, 4, 0x80, 0xFF)
    steps = range(256) if rm.FULL else (0, 1, 2, 0x0F, 0x10, 0x11, 0x12, 0x1F, 0x20, 0x21, 0xFE, 0xFF)
    ticks = []
    # a) the state switch: every +4 value, flag 0x3B and +0.
    for b04, f793, b00 in itertools.product(states, (0, 1, 0xFE, 0xFF), (0, 2)):
        ticks.append(((b00, b04, 0), 0, f793, (360.0, 270.0, 240.0), (5, 7), (0x77, 0x66), 0))
    # b) the D_00810813 dispatch x sub-state x poll result x inside/outside.
    dispatch = []
    for d813, b05, poll, where, spot in itertools.product(steps, (0, 1, 2, 0xFF), (0, 1, 3), (0, 1), range(3)):
        x, z = (inside_xz if where else outside_xz)[spot]
        dispatch.append(((1, 1, b05), d813, 0, (x, (270.0, 280.0, 290.0)[spot], z), (5, 0xFF), (0x77, 0x66), poll))
    chosen = rm.select(dispatch, 500, 0x825468, axes=(lambda c: c[1], lambda c: c[0][2], lambda c: c[6],
                                                      lambda c: c[3]))
    ticks += chosen
    # c) every gate edge for every beat step, inside and outside.
    for d813, y, where in itertools.product((0, 1, 0x10, 0x11, 0x20), heights, (0, 1)):
        beat = {0: 0, 1: 0, 0x10: 1, 0x11: 1, 0x20: 2}[d813]
        x, z = (inside_xz if where else outside_xz)[beat]
        ticks.append(((1, 1, 0), d813, 0, (x, y, z), (5, 7), (0x77, 0x66), 0))
    # d) points around each quad at the passing heights (integrated geometry).
    for beat, d813 in ((0, 0), (0, 1), (1, 0x10), (1, 0x11), (2, 0x20)):
        pts = quad_points(QUADS[beat], random.Random(beat), rm.pick(200, 0))
        pts = pts if rm.FULL else pts[::7]
        y = (270.0, 280.0, 290.0)[beat]
        ticks += [((1, 1, 0), d813, 0, (x, y, z), (5, 7), (0x77, 0x66), 0) for x, z in pts]
    ticks.append(((1, 1, 0), 0x10, 0, (math.nan, 280.0, 284.0), (5, 7), (0x77, 0x66), 0))
    outcomes = rm.parallel_map(protocol_case, ticks)
    tally = {k: outcomes.count(k) for k in sorted(set(outcomes))}
    total_ticks = len(ticks) - len(chosen) + len(dispatch)

    faults = fault_cases()
    for label, node, d813, poll, drop, fail, address in faults:
        rc, fault, *_ = native_tick(node, d813, 0, (360.0, 270.0, 240.0), (5, 7), (0, 0), poll, drop, fail)
        assert rc < 0 and fault == address, (label, rc, hex(fault), hex(address))

    # 2. 001B1EA0 standalone (original 001B1EA0/0011E620/0011C4C8 executed).
    geometry = []
    for q in range(4):
        pts = quad_points(QUADS[q] if q < 3 else 0x82AB80, rng, rm.pick(400, 16))
        geometry += [(q, (x, 265.0, z), 4, 0) for x, z in pts]
    edge = [(0, (385.0, 0.0, 240.0)), (1, (452.0, 0.0, 282.0)), (1, (481.0, 0.0, 287.0)), (2, (410.0, 0.0, 190.0))]
    for (q, p), count in itertools.product(edge, (-1, 0, 1, 2, 3)):
        geometry.append((q, p, count, 0))
    for (q, p), mode in itertools.product(edge, (1, 2, 3, -1, 0x7FFFFFFF)):
        geometry.append((q, p, 4, mode))
    geometry += [(1, (math.nan, 0.0, 285.0), 4, 0), (1, (476.0, 0.0, math.inf), 4, 0)]
    kept = rm.select(geometry, 600, 0x82AC20,
                     keep=lambda i, g: g[2] != 4 or g[3] != 0 or not math.isfinite(g[1][0] + g[1][2]) or g[0] == 1)
    results = rm.parallel_map(geometry_case, kept)

    # 3. Route.
    route = route_replay()
    assert [s[2] for s in route['starts']] == [hex(s) for s in SCRIPTS], route['starts']
    assert [c[2] for c in route['completions']] == ['0x10', '0x20', '0xff'], route['completions']

    rm.banner(rm.part(len(atans), atan_total, 'SDK atan cases'), rm.part(len(ticks), total_ticks, 'owner ticks'),
              rm.part(len(kept), len(geometry), 'polygon cases'), f'{route["frames"]} route frames')
    print(f'SDK atan2f 0011E620/0011C4C8/0011DBB8: PASS ({len(atans)} cases, bit-identical)')
    print(f'director 0x8253F0 ticks: PASS {tally}; {len(faults)} fail-stop paths')
    print(f'001B1EA0 mode 0: PASS ({len(kept)} cases: {results.count(1)} inside, {results.count(0)} outside, '
          f'{results.count("refused")} refused)')
    print(f'route replay: PASS ({route["frames"]} frames, {route["gate_tests"]} gate tests, {route["polls"]} polls; '
          f'starts {route["starts"]}; completions {route["completions"]}; D_00810CC4 0 -> 1 across beat 10)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
