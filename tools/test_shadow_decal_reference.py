#!/usr/bin/env python3
"""Execute the original decal packet 001CE300 (and its clipper) and compare
em_shadow_decal_original.c byte for byte.

docs/SHADOW_DECAL.md. The user's pinned ELF and the captured route RAM
supply every instruction and every table; none are embedded here.

Original code executed unmodified, callees included (nothing is hooked
inside 001CE300):
  001CE300  the decal's GS packet
  001CF470  the frustum clipper   001CF870 edge outcode   001CF970 crossing
  001CB950  the TEX0 A+D packet
  001CD370, 001CB5F0, 001CB900 (001CB6B0, 001CB9B0), 0011DF78, 00121870
The native side runs em_shadow_decal_001CE300 bound through
em_shadow_decal_bind_packet_chain to em_packet_chain_original over a copy of
the same RAM, so the packet-chain binding is exercised too.

Every 001CE300 case compares ALL of EE RAM (32 MiB: the packets, the page
slot and head words, the render-context cursor, the clipper buffers
D_008112C0..D_00811CBF) and all of the scratchpad afterwards, byte for byte,
plus the return value.

Cases:
  route  0015BF90 runs as original over every in-scope route beat (00..14)
         and the opening capture (0019A570 over the captured collision
         world, 0011E620, 001CD390 ... all original); its 001CE300 call is
         caught with the state at entry, the original 001CE300 runs from
         there, and the native one from a copy. Variants: as captured, and
         the 0x41 scripted path.
  unit   001CE300 called directly over the RAM of beats 02 / 04 / 05 / 08
         with generated quads (decal-shaped squares of many sizes and
         orientations around the player, and free corners at radii 4 ..
         4000 that cross every clip plane and w = 0), varied tag / TEX0 /
         colour, junk in the stale record fields, other beats' cameras,
         other fog coefficients (a negative fog maximum included).
  clip   001CF470 alone on the census generator of
         tools/test_census_unverified_reference.py (same seed, same cases):
         the native fan must equal the original's, and its digest must equal
         the census's pinned CF470_REFERENCE.
  leaf   001CF870 and 001CF970 alone on random records (signed zeros,
         denormals, NaN / Inf patterns, equal ends, every axis, both signs,
         the output aliasing an input).
Every conditional branch of the five routines is taken both ways, except
two outcomes that cannot occur (asserted absent-by-construction below).
Native fault cases: every missing worker / scratch / stage / argument, a
failing worker at each call position, the latched refusal.

The decal texture boundary: the captured TEX0 is the documented word, and
the GS memory at TBP0 0x2469 (PSMT8 16x16) and CBP 0x2148 is resident and
identical in every checked beat (digests compared across beats, nothing
printed or stored).

Default run ~10 s; EM_TEST_FULL=1: the exhaustive sweeps.
"""
import ctypes as C
import hashlib
import math
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM  # noqa: E402
import ee_float_model as M  # noqa: E402
import test_player_slide_reference as shared  # noqa: E402
import test_shadow_actor_route_reference as SAR  # noqa: E402
from test_player_slide_reference import EE, read_elf, sx32, s32  # noqa: E402

MASK, MASK64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route'
REFERENCE = DECOMP / 'build/startup-reference'
OUT = ROOT / 'build' / os.environ.get('EM_LANE', 'shadow_decal_reference')

CE300, CF470, CF870, CF970, CB950 = 0x1CE300, 0x1CF470, 0x1CF870, 0x1CF970, 0x1CB950
SIZES = {CE300: 0x358, CF470: 0x3F4, CF870: 0xF4, CF970: 0xE4, CB950: 0x60}
CALLEES = {0x1CD370, CF470, 0x1CB5F0, CB950, 0x1CB900, 0x11DF78, 0x121870, CF870, CF970}
PLAYER = 0x8102B0
STAGE, STAGE_BYTES = 0x8112C0, 0xA00
TEX0 = 0x2004290511322469
QUAD = 0x7F0F8000            # the interpreter's private stack region: RAM stays untouched
UNIT_BEATS = ('02_elevator_refusal', '04_elevator_ride', '05_boxes', '08_truck_crossing')
# Outcomes that cannot occur: 001CE300's last corner test (corner 0) failing
# needs a corner outside 0..3; 001CF470's last outcode test (code 0) failing
# needs a lane above |w| and below -|w| at once.
IMPOSSIBLE = {(0x1CE408, False), (0x1CF5EC, False)}


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def fbits(value):
    return shared.bits(C.c_float(value).value)


def in_ranges(pc):
    return any(start <= pc < start + size for start, size in SIZES.items())


# ======================================================================
# The interpreter: the shadow-route EE (every COP1 / VU0 op from
# ee_float_model) plus the MMI three-word rotate, branch outcomes of the
# five routines at any depth.
# ======================================================================

class DecalEE(SAR.RouteEE):
    def __init__(self, elf, ram=None, spad=None):
        super().__init__(elf, ram, spad)
        self.decal_outcomes = set()

    def branch(self, word, pc):
        b = EE.branch(self, word, pc)
        if b is not None and in_ranges(pc):
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if not (op == 4 and rs == 0 and rt == 0):
                self.decal_outcomes.add((pc, b[0]))
        return b

    def mmi(self, word, pc):
        fn, sub, rt, rd = word & 63, word >> 6 & 31, word >> 16 & 31, word >> 11 & 31
        if fn == 0x09 and sub == 0x1F:          # three-word rotate: word 0 <- 1, 1 <- 2, 2 <- 0
            value = (self.r[rt] & MASK64) | (self.rh[rt] << 64)
            w = [(value >> (32 * i)) & MASK for i in range(4)]
            w = [w[1], w[2], w[0], w[3]]
            out = sum(v << (32 * i) for i, v in enumerate(w))
            if rd:
                self.r[rd], self.rh[rd] = out & MASK64, out >> 64
            return
        super().mmi(word, pc)


def run_ce300(ee, tag, quad_address, tex0, rgba):
    """001CE300 with a full 64-bit a2 (the TEX0 word), hooks off, the
    interrupted registers restored afterwards."""
    saved = (list(ee.r), list(ee.rh), ee.hi, ee.lo, list(ee.f), ee.acc,
             ee.cond, [list(v) for v in ee.vf], list(ee.vacc), ee.q)
    hooks, ee.hooks = ee.hooks, {}
    try:
        ee.r[29] = (ee.r[29] - 0x400) & ~15
        ee.r[4], ee.r[5] = sx32(tag) & MASK64, sx32(quad_address) & MASK64
        ee.r[6], ee.rh[6] = tex0 & MASK64, 0
        ee.r[7] = sx32(rgba) & MASK64
        ee.r[31] = shared.RETURN
        ee.run(CE300)
    finally:
        ee.hooks = hooks
    (ee.r, ee.rh, ee.hi, ee.lo, ee.f, ee.acc, ee.cond, ee.vf, ee.vacc, ee.q) = saved


# ======================================================================
# Native side
# ======================================================================

P, U8, U32, I32 = C.POINTER, C.c_uint8, C.c_uint32, C.c_int32


class Region(C.Structure):
    _fields_ = [('base', U32), ('size', U32), ('bytes', P(U8))]


class Chain(C.Structure):
    _fields_ = [('regions', P(Region)), ('region_count', C.c_uint), ('d275670', U32), ('d275674', U32),
                ('fault', I32), ('fault_function', U32), ('fault_address', U32)]


CD370_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, I32, P(U32))
CB5F0_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, U32, I32, I32, P(P(U8)))
CB900_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, U32, I32, I32)
FOG_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, P(U32))


class Workers(C.Structure):
    _fields_ = [('ctx', C.c_void_p), ('w_001CD370', CD370_FN), ('w_001CB5F0', CB5F0_FN),
                ('w_001CB900', CB900_FN), ('fog', FOG_FN)]


class Scratch(C.Structure):
    _fields_ = [('s3600', P(U32)), ('s3AC0', P(U32))]


class Fault(C.Structure):
    _fields_ = [('address', U32), ('code', I32)]


class Decal(C.Structure):
    _fields_ = [('stage', P(U32)), ('scratch', Scratch), ('workers', P(Workers)), ('fault', Fault)]


NATIVE = None


def build_native():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = OUT / ('shadow_decal.dylib' if sys.platform == 'darwin' else 'shadow_decal.so')
    sources = ['src/game/em_shadow_decal_original.c', 'src/game/em_packet_chain_original.c',
               'src/game/em_status_ui_leftovers.c', 'src/game/em_sdk_math_original.c']
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    '-ffp-contract=off', '-shared', '-fPIC', '-Isrc'] + sources + ['-lm', '-o', str(lib)],
                   cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    D = P(Decal)
    native.em_packet_chain_init.argtypes = [P(Chain), P(Region), C.c_uint, U32, U32]
    native.em_shadow_decal_bind_packet_chain.argtypes = [P(Workers), P(Chain)]
    native.em_shadow_decal_001CE300.argtypes = [D, I32, P(U32), C.c_uint64, U32]
    native.em_shadow_decal_w_001CE300.argtypes = [C.c_void_p, I32, P(U32), C.c_uint64, U32]
    native.em_shadow_decal_001CF470.argtypes = [D, P(U32), P(I32)]
    native.em_shadow_decal_001CF870.argtypes = [P(U32), P(U32), I32]
    native.em_shadow_decal_001CF870.restype = I32
    native.em_shadow_decal_001CF970.argtypes = [P(U32), P(U32), P(U32), I32, U32]
    native.em_shadow_decal_001CB950.argtypes = [D, U32, I32, C.c_uint64]
    return native


def arr(kind, values):
    return (kind * len(values))(*values)


class NativeState:
    """One native run over copies of `mem` / `spad`, bound through the
    packet chain (whole RAM as one region). `wrap` may replace workers."""

    def __init__(self, mem, spad, wrap=None):
        self.ram = (U8 * len(mem)).from_buffer_copy(mem)
        self.spad = (U8 * len(spad)).from_buffer_copy(spad)
        d275670, d275674 = struct.unpack_from('<II', mem, 0x275670)
        self.region = Region(0, len(mem), C.cast(self.ram, P(U8)))
        self.chain = Chain()
        NATIVE.em_packet_chain_init(C.byref(self.chain), C.byref(self.region), 1, d275670, d275674)
        self.workers = Workers()
        NATIVE.em_shadow_decal_bind_packet_chain(C.byref(self.workers), C.byref(self.chain))
        assert self.workers.ctx == C.addressof(self.chain)
        if wrap:
            wrap(self)
        base, sbase = C.addressof(self.ram), C.addressof(self.spad)
        self.decal = Decal(C.cast(base + STAGE, P(U32)),
                           Scratch(C.cast(sbase + 0x3600, P(U32)), C.cast(sbase + 0x3AC0, P(U32))),
                           C.pointer(self.workers), Fault(0, 0))

    def ce300(self, tag, quad, tex0, rgba):
        return NATIVE.em_shadow_decal_001CE300(C.byref(self.decal), tag, arr(U32, quad), tex0, rgba)


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


def compare_state(where, rc, native, mem, spad):
    assert rc == 0, (where, 'native faulted', rc, hex(native.decal.fault.address), native.decal.fault.code,
                     native.chain.fault, hex(native.chain.fault_function))
    got_ram, got_spad = bytes(native.ram), bytes(native.spad)
    assert got_ram == mem, (where, 'RAM differs (address, native, original)', first_differences(got_ram, mem))
    assert got_spad == spad, (where, 'scratchpad differs', first_differences(got_spad, spad, 0x70000000))


# ======================================================================
# Cases
# ======================================================================

IMAGES = {}
ELF = None


def image(beat):
    if beat not in IMAGES:
        if beat == 'opening':
            IMAGES[beat] = ((REFERENCE / 'opening_ee.bin').read_bytes(),
                            (REFERENCE / 'opening_scratchpad.bin').read_bytes())
        else:
            IMAGES[beat] = ((ROUTE / beat / 'eeMemory.bin').read_bytes(),
                            (ROUTE / beat / 'scratchpad.bin').read_bytes())
    return IMAGES[beat]


def check_code(ram, where):
    base = EE(ELF)
    spans = list(SIZES.items()) + [(a, 0x40) for a in CALLEES - set(SIZES)]
    for start, size in spans:
        assert ram[start:start + size] == bytes(base.mem[start:start + size]), (where, hex(start))


def detach(field, kind):
    """A callable for the function pointer a Workers field holds now (a
    field read shares the structure's memory, so it would follow a later
    assignment to the same field)."""
    return kind(C.cast(field, C.c_void_p).value)


def packet_counts(native):
    """Wrap w_001CB5F0 to record every packet size (quadwords) it opens."""
    counts = []
    inner = detach(native.workers.w_001CB5F0, CB5F0_FN)

    def w(ctx, table, id_, count, out):
        counts.append(count)
        return inner(ctx, table, id_, count, out)
    native._keep = CB5F0_FN(w)
    native.workers.w_001CB5F0 = native._keep
    return counts


def route_case(item):
    beat, variant = item
    ram, spad = image(beat)
    check_code(ram, beat)
    ee = DecalEE(ELF, ram, spad)
    if variant == 'x41':
        ee.save(PLAYER + 0x1F0, 0x41, 1)
        ee.save(0x70003B8D, 1, 1)
    on_actor = ee.load(PLAYER + 0x214) != 0
    seen = []

    def hook(e):
        tag, quad_address = s32(e.r[4]), e.r[5] & MASK
        tex0, rgba = e.r[6] & MASK64, e.r[7] & MASK
        quad = [e.load(quad_address + 4 * i) for i in range(16)]
        mem0, spad0 = bytes(e.mem), bytes(e.spad)
        run_ce300(e, tag, quad_address, tex0, rgba)
        seen.append((tag, quad, tex0, rgba, mem0, spad0, bytes(e.mem), bytes(e.spad)))
    ee.hooks = {CE300: hook}
    ee.call(0x15BF90, (PLAYER,))
    rows = []
    for tag, quad, tex0, rgba, mem0, spad0, mem1, spad1 in seen:
        assert (tag, tex0) == (1, TEX0), (beat, variant, tag, hex(tex0))
        native = NativeState(mem0, spad0)
        counts = packet_counts(native)
        rc = native.ce300(tag, quad, tex0, rgba)
        compare_state((beat, variant), rc, native, mem1, spad1)
        rows.append(([(c - 2) // 3 for c in counts[:-1]], hex(rgba)))
    return beat, variant, on_actor, rows, ee.decal_outcomes


def player_point(ram):
    player = struct.unpack_from('<I', ram, 0x275B44)[0] or PLAYER
    return [shared.number(w) for w in struct.unpack_from('<3I', ram, player + 0xB0)]


SPECIAL = [0x7FC00000, 0xFFC00000, 0x7F800000, 0xFF800000, 0x80000000, 0x00000001, 0x7F7FFFFF,
           0x807FFFFF, 0xFFFFFFFF, 0x00000000]


def random_quad(rng, ram):
    px, py, pz = player_point(ram)
    kind = rng.random()
    if kind < 0.45:                        # decal-shaped: a square on a plane near the player
        cx, cy, cz = (px + rng.uniform(-30, 30), py + rng.uniform(-20, 5), pz + rng.uniform(-30, 30))
        hw = rng.choice((4.2, 4.2, rng.uniform(0.2, 12.0), rng.uniform(20, 400), 3000.0))
        hh = hw if rng.random() < 0.8 else rng.uniform(0.2, 50.0)
        a, b = rng.uniform(-3.2, 3.2), rng.uniform(-1.6, 1.6)
        u = (math.cos(a), 0.0, math.sin(a))
        v = (-u[2] * math.sin(b), math.cos(b), u[0] * math.sin(b))
        corners = []
        for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
            corners.append((cx + sx * hw * u[0] + sy * hh * v[0], cy + sx * hw * u[1] + sy * hh * v[1],
                            cz + sx * hw * u[2] + sy * hh * v[2]))
    else:                                  # free corners crossing the clip planes
        radius = rng.choice((4.0, 40.0, 400.0, 4000.0, 40000.0))
        corners = [(px + rng.uniform(-radius, radius), py + rng.uniform(-radius, radius) * rng.choice((0.25, 1.0)),
                    pz + rng.uniform(-radius, radius)) for _ in range(4)]
    words = []
    for c in corners:
        words += [fbits(x) for x in c] + [rng.choice((F(1.0), F(1.0), 0, rng.getrandbits(32)))]
    if rng.random() < 0.03:
        words[rng.randrange(16)] = rng.choice(SPECIAL)
    return words


def unit_case(seed):
    rng = random.Random('ce300:%d' % seed)
    beat = UNIT_BEATS[seed % len(UNIT_BEATS)]
    ram, spad = image(beat)
    ee = DecalEE(ELF, ram, spad)
    if rng.random() < 0.3:                 # another beat's camera
        other = image(rng.choice(UNIT_BEATS))[1]
        ee.write(0x70003AC0, other[0x3AC0:0x3B00])
    if rng.random() < 0.3:                 # junk in the clipper records' stale fields
        for k in range(STAGE_BYTES // 4):
            if rng.random() < 0.5:
                ee.save(STAGE + 4 * k, rng.choice(SPECIAL) if rng.random() < 0.1 else rng.getrandbits(32))
    if rng.random() < 0.15:                # other fog coefficients (max, -, bias, scale), a negative max too
        ctx = struct.unpack_from('<I', ram, 0x275670)[0]
        fog = [F(rng.choice((255.0, rng.uniform(-300, 300)))), rng.getrandbits(32),
               F(rng.uniform(-400, 400)), F(rng.uniform(-3, 3))]
        for k, w in enumerate(fog):
            ee.save(ctx + 0xA0 + 4 * k, w)
    for k in range(4):                     # the colour words start as junk
        ee.save(0x70003600 + 4 * k, rng.getrandbits(32))
    quad = random_quad(rng, ram)
    for i, w in enumerate(quad):
        ee.save(QUAD + 4 * i, w)
    tag = rng.choice((1, 1, 1, 0, 2, 3, 4, 5, -1))
    tex0 = TEX0 if rng.random() < 0.6 else rng.getrandbits(64)
    rgba = rng.choice((0xA2050505, 0xFF080808, rng.getrandbits(32)))
    mem0, spad0 = bytes(ee.mem), bytes(ee.spad)
    run_ce300(ee, tag, QUAD, tex0, rgba)
    native = NativeState(mem0, spad0)
    counts = packet_counts(native)
    rc = native.ce300(tag, quad, tex0, rgba)
    compare_state(('unit', seed, beat), rc, native, bytes(ee.mem), bytes(ee.spad))
    return ee.decal_outcomes, tuple((c - 2) // 3 for c in counts[:-1])


def clip_census():
    """The census generator (test_census_unverified_reference.part_1cf470):
    native fans equal the original's, and the digest equals its pin."""
    import test_census_unverified_reference as census
    beat = [b for b in BEATS if b.startswith('02_')][0]
    ram, spad = image(beat)
    ctx = struct.unpack_from('<I', ram, 0x275670)[0]
    mtx = ctx + 0x2240
    m = arr(U32, struct.unpack_from('<16I', ram, mtx))
    px, py, pz = player_point(ram)
    rng = random.Random(0xCF470)
    n = RM.pick(400, 40)
    histogram, digest, outcomes = {}, hashlib.sha256(), set()
    for i in range(n):
        radius = (4.0, 40.0, 400.0, 4000.0)[i % 4]
        o = DecalEE(ELF, ram, spad)
        for v in range(3):
            base = STAGE + 0x50 * v
            for k in range(0x50 // 4):
                o.save(base + 4 * k, 0)
            pos = (px + rng.uniform(-radius, radius), py + rng.uniform(-radius, radius) * 0.25,
                   pz + rng.uniform(-radius, radius), 1.0)
            for k, x in enumerate(pos):
                o.save(base + 4 * k, fbits(x))
            o.save(base + 0x30, fbits(float(v & 1)))
            o.save(base + 0x34, fbits(float(v >> 1)))
        stage0 = o.read(STAGE, STAGE_BYTES)
        o.call(CF470, (STAGE, mtx))
        count = s32(o.r[2])
        stage = (U32 * (STAGE_BYTES // 4)).from_buffer_copy(stage0)
        d = Decal(C.cast(stage, P(U32)), Scratch(None, None), None, Fault(0, 0))
        got = I32(-7)
        rc = NATIVE.em_shadow_decal_001CF470(C.byref(d), m, C.byref(got))
        assert rc == 0 and got.value == count, ('001CF470 census case', i, rc, got.value, count)
        assert bytes(stage) == o.read(STAGE, STAGE_BYTES), ('001CF470 census stage', i,
                                                           first_differences(bytes(stage), o.read(STAGE, STAGE_BYTES),
                                                                             STAGE))
        histogram[count] = histogram.get(count, 0) + 1
        digest.update(struct.pack('<i', count) + o.read(0x8117C0, 0x50 * count))
        outcomes |= o.decal_outcomes
    histogram = dict(sorted(histogram.items()))
    want_hist, want_digest = census.CF470_REFERENCE[RM.MODE]
    assert histogram == want_hist, ('census fan sizes', histogram, want_hist)
    assert digest.hexdigest()[:32] == want_digest, ('census digest', digest.hexdigest())
    return n, histogram, outcomes


def random_record(rng):
    words = []
    for _ in range(20):
        r = rng.random()
        if r < 0.08: words.append(rng.choice(SPECIAL))
        elif r < 0.12: words.append(rng.getrandbits(32))
        else: words.append(F(rng.uniform(-3000, 3000) if rng.random() < 0.5 else rng.uniform(-2, 2)))
    return words


REC_A, REC_B, REC_OUT = 0x6D0000, 0x6D0100, 0x6D0200


def leaf_case(seed):
    rng = random.Random('leaf:%d' % seed)
    ee = DecalEE(ELF)
    a, b = random_record(rng), random_record(rng)
    kind = rng.random()
    if kind < 0.1:
        b = list(a)                                         # a degenerate edge
    elif kind < 0.2:
        for k in range(16, 20):                              # a clip lane exactly on the plane
            b[k] = a[k]
        b[16 + rng.randrange(3)] = rng.choice((a[19], a[19] ^ 0x80000000))
    elif kind < 0.3:
        a[19] = rng.choice((0, 0x80000000, 0x00000001))      # w = +-0 or a denormal
    for i, w in enumerate(a): ee.save(REC_A + 4 * i, w)
    for i, w in enumerate(b): ee.save(REC_B + 4 * i, w)
    axis = rng.choice((0, 1, 2, 0, 1, 2, 3)) if rng.random() < 0.5 else rng.randrange(3)
    ee.call(CF870, (REC_A, REC_B, axis))
    code = s32(ee.r[2])
    got = NATIVE.em_shadow_decal_001CF870(arr(U32, a), arr(U32, b), axis)
    assert got == code, ('001CF870', seed, axis, got, code)
    axis = rng.randrange(3)
    sign = rng.choice((F(1.0), F(-1.0), F(1.0), F(-1.0), rng.choice(SPECIAL), F(rng.uniform(-3, 3))))
    alias = rng.random()
    out = REC_A if alias < 0.1 else REC_B if alias < 0.2 else REC_OUT
    ee.f[12] = sign
    ee.call(CF970, (out, REC_A, REC_B, axis))
    want = [ee.load(out + 4 * i) for i in range(20)]
    na, nb = arr(U32, a), arr(U32, b)
    target = na if alias < 0.1 else nb if alias < 0.2 else arr(U32, [0] * 20)
    rc = NATIVE.em_shadow_decal_001CF970(target, na, nb, axis, sign)
    assert rc == 0 and list(target) == want, ('001CF970', seed, axis, hex(sign),
                                               [hex(x) for x in target], [hex(x) for x in want])
    return ee.decal_outcomes


# ---- native fault cases -----------------------------------------------------

def fault_cases():
    beat = UNIT_BEATS[1]
    ram, spad = image(beat)
    ee = DecalEE(ELF, ram, spad)
    # a quad around the player that both triangles keep (the route's own quad)
    route = route_case((beat, 'play'))
    assert route[3] and all(len(c[0]) == 2 and min(c[0]) > 0 for c in route[3]), route[3]
    captured = []
    ee.hooks = {CE300: lambda e: captured.append(([e.load((e.r[5] & MASK) + 4 * i) for i in range(16)],
                                                  e.r[7] & MASK))}
    ee.call(0x15BF90, (PLAYER,))
    quad, rgba = captured[0]
    mem0, spad0 = ram, spad
    count = 0
    # missing pieces: UNBOUND at 001CE300, nothing written
    for missing in ('w_001CD370', 'w_001CB5F0', 'w_001CB900', 'fog', 's3600', 's3AC0', 'stage', 'workers',
                    'quad'):
        n = NativeState(mem0, spad0)
        if missing in ('s3600', 's3AC0'):
            setattr(n.decal.scratch, missing, None)
        elif missing == 'stage':
            n.decal.stage = None
        elif missing == 'workers':
            n.decal.workers = None
        elif missing != 'quad':
            setattr(n.workers, missing, type(getattr(n.workers, missing))())
        if missing == 'quad':
            rc = NATIVE.em_shadow_decal_001CE300(C.byref(n.decal), 1, None, TEX0, rgba)
        else:
            rc = n.ce300(1, quad, TEX0, rgba)
        assert rc == -1 and n.decal.fault.code == 1 and n.decal.fault.address == CE300, (missing, rc)
        assert bytes(n.ram) == mem0 and bytes(n.spad) == spad0, missing
        count += 1
    # a failing worker at each call position
    order = []
    probe = NativeState(mem0, spad0, wrap=lambda s: recorder(s, order, None))
    assert probe.ce300(1, quad, TEX0, rgba) == 0
    want = ['w_001CD370', 'w_001CB5F0', 'fog', 'w_001CD370', 'w_001CB5F0', 'fog', 'w_001CB5F0', 'w_001CB900']
    assert order == want, order
    addresses = {'w_001CD370': 0x1CD370, 'w_001CB5F0': 0x1CB5F0, 'fog': 0x1CE50C, 'w_001CB900': 0x1CB900}
    for k, name in enumerate(order):
        calls = []
        n = NativeState(mem0, spad0, wrap=lambda s, c=calls, k=k: recorder(s, c, k))
        rc = n.ce300(1, quad, TEX0, rgba)
        assert rc == -1 and n.decal.fault.code == 2 and n.decal.fault.address == addresses[name], \
            (k, name, rc, hex(n.decal.fault.address), n.decal.fault.code)
        assert len(calls) == k + 1, (k, calls)
        before = (bytes(n.ram), bytes(n.spad))
        assert n.ce300(1, quad, TEX0, rgba) == -1 and (bytes(n.ram), bytes(n.spad)) == before
        assert len(calls) == k + 1
        count += 1
    # the leaves refuse what the original cannot do
    z = arr(U32, [0] * 20)
    assert NATIVE.em_shadow_decal_001CF870(z, z, -1) == -1 and NATIVE.em_shadow_decal_001CF870(z, z, 4) == -1
    assert NATIVE.em_shadow_decal_001CF970(z, z, z, -1, F(1.0)) == -1
    # the adapter is the entry point
    n = NativeState(mem0, spad0)
    rc = NATIVE.em_shadow_decal_w_001CE300(C.addressof(n.decal), 1, arr(U32, quad), TEX0, rgba)
    m = NativeState(mem0, spad0)
    assert rc == 0 and m.ce300(1, quad, TEX0, rgba) == 0 and bytes(n.ram) == bytes(m.ram)
    return count + 2


def recorder(state, calls, fail_at):
    """Wrap every worker: record its name, fail (return -1) at call fail_at."""
    for name, kind in (('w_001CD370', CD370_FN), ('w_001CB5F0', CB5F0_FN), ('w_001CB900', CB900_FN),
                       ('fog', FOG_FN)):
        inner = detach(getattr(state.workers, name), kind)

        def w(*args, name=name, inner=inner):
            calls.append(name)
            if fail_at is not None and len(calls) - 1 == fail_at:
                return -1
            return inner(*args)
        fn = kind(w)
        setattr(state, '_keep_' + name, fn)
        setattr(state.workers, name, fn)


# ---- the texture boundary -------------------------------------------------------

def texture_boundary(beats):
    sys.path.insert(0, str(DECOMP / 'tools'))
    from gs_vram import read_localmem
    from clut_pair import read_psmt8, read_clut_rgba
    tbp0, tbw = TEX0 & 0x3FFF, (TEX0 >> 14) & 0x3F
    psm, tw, th = (TEX0 >> 20) & 0x3F, 1 << ((TEX0 >> 26) & 0xF), 1 << ((TEX0 >> 30) & 0xF)
    cbp, cpsm, csm, csa = (TEX0 >> 37) & 0x3FFF, (TEX0 >> 51) & 0xF, (TEX0 >> 55) & 1, (TEX0 >> 56) & 0x1F
    assert (tbp0, tbw, psm, tw, th, cbp, cpsm, csm, csa) == (0x2469, 8, 0x13, 16, 16, 0x2148, 0, 0, 0)
    assert (TEX0 >> 34) & 1 == 1 and (TEX0 >> 35) & 3 == 0 and (TEX0 >> 61) == 1
    digests, top = set(), 0
    for beat in beats:
        gs = ROUTE / beat / 'gs.bin'
        _, lm = read_localmem(gs)
        texels = read_psmt8(lm, tbp0, tbw, tw, th)
        clut = read_clut_rgba(lm, cbp)
        digests.add((hashlib.sha256(texels).hexdigest(), hashlib.sha256(clut).hexdigest()))
        top = max(top, max(texels))
    assert len(digests) == 1, ('decal texture differs between beats', len(digests))
    return len(beats), top


# ======================================================================

BEATS = sorted(p.name for p in ROUTE.iterdir()
               if (p / 'eeMemory.bin').exists() and RM.in_scope_beat(p.name)) if ROUTE.exists() else []


def main():
    global ELF, NATIVE
    ELF = read_elf()
    NATIVE = build_native()
    for beat in UNIT_BEATS:
        image(beat)

    # the callee set of the five routines is exactly the expected one
    probe = EE(ELF)
    targets = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = probe.load(pc)
            if word >> 26 in (2, 3):
                target = (word & 0x3FFFFFF) << 2
                if not start <= target < start + size:
                    targets.add(target)
    assert targets == CALLEES, ('callee set', sorted(map(hex, targets ^ CALLEES)))

    outcomes = set()
    leaf_items = RM.select(range(50000), 1500, 7)
    for got in RM.parallel_map(leaf_case, leaf_items):
        outcomes |= got
    n_clip, histogram, got = clip_census()
    outcomes |= got
    unit_items = RM.select(range(20000), 240, 9)
    fans = {}
    for got, counts in RM.parallel_map(unit_case, unit_items):
        outcomes |= got
        for c in counts:
            fans[c] = fans.get(c, 0) + 1

    route_items = [(b, v) for b in BEATS for v in ('play', 'x41')]
    if (REFERENCE / 'opening_ee.bin').exists():
        route_items.append(('opening', 'play'))
    rows = RM.parallel_map(route_case, route_items, cost=lambda it: it[0] in UNIT_BEATS)
    packets = 0
    for beat, variant, on_actor, calls, got in rows:
        outcomes |= got
        packets += len(calls)
        if variant == 'play':
            desc = '; '.join(f'fans {c} rgba {r}' for c, r in calls) or 'no 001CE300 call'
            print(f'  {beat}: +0x214 {"set" if on_actor else "0"}, {desc}')

    branches = set()
    for start, size in SIZES.items():
        for pc in range(start, start + size, 4):
            word = probe.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or (op == 17 and rs == 8):
                if not (op == 4 and rs == 0 and rt == 0):
                    branches.add(pc)
    want = {(pc, t) for pc in branches for t in (True, False)} - IMPOSSIBLE
    missing = sorted((hex(pc), t) for pc, t in want - outcomes)
    assert not missing, ('branch outcomes never seen', missing)
    assert not (outcomes & IMPOSSIBLE), 'an outcome thought impossible occurred'

    faults = fault_cases()
    tex_beats, top = texture_boundary(BEATS if RM.FULL else [b for b in BEATS if b in UNIT_BEATS])

    RM.banner(
        RM.part(len(leaf_items), 50000, '001CF870 + 001CF970 leaf cases'),
        f'{n_clip} census 001CF470 cases (fans {histogram}, digest = the census pin)',
        RM.part(len(unit_items), 20000, '001CE300 unit cases') + f' (packet fan sizes {dict(sorted(fans.items()))})',
        f'{len(route_items)} captured-RAM route cases ({packets} 001CE300 calls, RAM + scratchpad byte-exact)',
        f'{len(branches)} conditional branches both ways (2 outcomes impossible by construction)',
        f'{faults} fault cases',
        f'decal texture resident and identical in {tex_beats} beats (indices 0..{top})')
    print('shadow decal reference: PASS')


if __name__ == '__main__':
    main()
