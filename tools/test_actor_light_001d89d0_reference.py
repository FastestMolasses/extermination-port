#!/usr/bin/env python3
"""Compare the actor lighting driver 001D89D0 (em_actor_light_001D89D0.c)
with the original code, and prove it bound as the owner draw's w_001D89D0.

The oracle is the owner-services EE interpreter (128-bit GPRs, COP1, VU0
macro mode; float arithmetic = tools/ee_float_model.py), extended here with
the four MMI lane interleaves 00102798 uses. It executes the ORIGINAL
instructions of the pinned SCUS-97112 boot ELF: 001D89D0, 001D8C30 (and its
jump table), 001D8130, 001D7B30, 001D2910, 001D2710, 001D8340, 001D8270,
001D8690 and the SDK VU0 routines they call.

A. Synthetic states. Every lighting mode (0..8, -1, INT_MIN, large), the
   camera-fill and glow bits, light point +0xB0 or a node, every fold-gate
   type and radii around 30.0, room keys that hit, repeat, miss or use the
   0x0F00 key, point lights with positive, zero, negative, -0 and denormal
   weights near and far from the point, arg3 equal to or apart from
   owner + 0x80, and special bit patterns (NaN, Inf, MAX, denormals, -0) in
   the float inputs. A, B (prefilled with a pattern), the rig record
   D_00817BC0 and D_00275688 must be equal, and the original may write
   nothing else outside its stack.
B. Captured states. Every owner with draw method 001CAA00 and a bank model
   in the s87 route captures 00..14 (the 255 owner-frames of
   test_owner_draw_reference lane D): the original 001D89D0 runs after
   001D8C20(0) as 001CA990 reaches it, over the captured RAM and scratchpad;
   the native must produce the same A, B, rig record and D_00275688.
C. The owner draw chain. For every one of those owner-frames the ORIGINAL
   001CAA00 runs; for the drawn ones (119 units) the NATIVE chain
   (em_owner_services_001CAA00 with em_owner_draw, em_load_veil_particles
   and this module bound through em_actor_light_w_001D89D0) must write the
   original unit byte for byte; A must equal the original's scratchpad
   0x70003400 and B the unit's colour payload (VU1 0x3F5); the rig record
   and D_00275688 must equal the original's. The unit is located in the
   captured display list and counted.
D. The fail-stop contract: NULL views, a node index outside the slots, the
   latch, and a view fault leaving every output untouched.

No original instruction bytes, disassembly or data are written by this file;
the report in build/ holds only counts, ids and hashes.
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

import test_owner_draw_reference as tod
import test_owner_services_reference as osr
from reference_mode import banner, part, pick, parallel_map
from test_owner_services_reference import (Owner, Bone, Scratch, Channel, Services, WORKER_TYPES,
                                           OWNER_BYTES, OWNER_FLOATS, MAX_BONES, M32, M64, M128, ONE,
                                           bits, number)

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
OUT = ROOT / 'build/actor_light_001d89d0_reference'

ENTRY = 0x1D89D0
CTX_PTR, RIG_PTR, RIG, RIG_SIZE = 0x275670, 0x275688, 0x817BC0, 0x130
TABLE, TABLE_SIZE, SEED, VIEW, AREA = 0x251C50, 45 * 0x78, 0x253170, 0x810610, 0x810700
A_SPR, B_SPR = 0x70003400, 0x70003440
CONTEXT, OWNER, MODEL, NODES, ARG3 = 0x600000, 0x7B0000, 0x900000, 0x960000, 0x7C0000
EXCLUDED = (0x3E, 0x3D, 0x17, 0x16, 0x15, 0x0D, 0x0B, 0x09, 0x08, 0x03)
SPECIALS = [0, 0x80000000, 0x00000001, 0x807FFFFF, 0x00800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000,
            0xFF800000, 0x7FC00000, 0xFFC00000, 0x7F800001, ONE, 0xBF800000]

P32 = C.POINTER(C.c_uint32)


# ---------------------------------------------------------------- oracle

class EE(osr.EE):
    """The owner-services interpreter plus the MMI lane interleaves of the
    4x4 transpose 00102798 (parallel extend lower/upper word, copy lower/upper
    doubleword)."""

    def plain(self, w, pc):
        if w >> 26 == 28 and w & 63 in (0x08, 0x09, 0x28, 0x29):
            rs, rt, rd, sub = w >> 21 & 31, w >> 16 & 31, w >> 11 & 31, w >> 6 & 31
            x, y = self.r[rs] & M128, self.r[rt] & M128
            lane = lambda v, i: v >> 32 * i & M32
            fn = w & 63
            if fn == 0x08 and sub == 0x12:       # extend lower words: rt0, rs0, rt1, rs1
                out = [lane(y, 0), lane(x, 0), lane(y, 1), lane(x, 1)]
            elif fn == 0x28 and sub == 0x12:     # extend upper words: rt2, rs2, rt3, rs3
                out = [lane(y, 2), lane(x, 2), lane(y, 3), lane(x, 3)]
            elif fn == 0x09 and sub == 0x0E:     # lower doublewords: rt.lo, rs.lo
                out = [lane(y, 0), lane(y, 1), lane(x, 0), lane(x, 1)]
            elif fn == 0x29 and sub == 0x0E:     # upper doublewords: rs.hi, rt.hi
                out = [lane(x, 2), lane(x, 3), lane(y, 2), lane(y, 3)]
            else:
                return super().plain(w, pc)
            self.set128(rd, sum(v << 32 * i for i, v in enumerate(out)))
            self.r[0] = 0
            return
        return super().plain(w, pc)


# ---------------------------------------------------------------- native side

class World(C.Structure):
    _fields_ = [('d00275688', P32), ('d00817BC0', P32), ('ctx_246C', C.POINTER(C.c_int32)),
                ('ctx_000C', P32), ('ctx_0220', P32), ('ctx_2380', P32),
                ('d00810700', C.POINTER(C.c_uint8)), ('d00251C50', P32), ('d00253170', P32),
                ('d00810610', P32)]


class LightOwner(C.Structure):
    _fields_ = [('cls', C.c_uint8), ('kind', C.c_uint8), ('pose_bone', C.c_uint8),
                ('rgb', C.c_uint32 * 4), ('pos', C.c_uint32 * 4), ('model_radius', P32),
                ('node_c0', C.POINTER(P32)), ('node_count', C.c_uint32)]


class Light(C.Structure):
    _fields_ = [('world', World), ('fault', osr.Fault)]


RGB_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Owner), P32)


class Binding(C.Structure):
    _fields_ = [('light', C.POINTER(Light)), ('ctx', C.c_void_p), ('w_owner_rgb', RGB_FN)]


def build_library():
    OUT.mkdir(parents=True, exist_ok=True)
    source = os.environ.get('EM_ACTOR_LIGHT_SOURCE', str(ROOT / 'src/game/em_actor_light_001D89D0.c'))
    path = OUT / (f'actor_light_{os.getpid()}.dylib' if 'EM_ACTOR_LIGHT_SOURCE' in os.environ
                  else 'actor_light.dylib')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-I' + str(ROOT / 'src'), source,
                    str(ROOT / 'src/game/em_owner_services_original.c'),
                    str(ROOT / 'src/game/em_owner_draw_original.c'),
                    str(ROOT / 'src/game/em_load_veil_particles.c'), '-o', str(path)], check=True)
    lib = C.CDLL(str(path))
    PL, PO = C.POINTER(Light), C.POINTER(LightOwner)
    lib.em_actor_light_001D89D0.argtypes = [PL, PO, P32, P32, P32]
    lib.em_actor_light_001D8C30.argtypes = [PL, C.c_int32, P32, P32, P32]
    lib.em_actor_light_001D8270.argtypes = [PL, PO, C.POINTER(C.c_int32)]
    lib.em_actor_light_001D7B30.argtypes = [PL, P32]
    lib.em_actor_light_001D8130.argtypes = [PL]
    lib.em_actor_light_001D8340.argtypes = [PL, PO, C.c_uint32, P32]
    lib.em_actor_light_001D8690.argtypes = [PL, P32, P32, P32]
    lib.em_actor_light_w_001D89D0.argtypes = [C.c_void_p, C.POINTER(Owner), C.POINTER(C.c_float),
                                              C.POINTER(C.c_float)]
    # What test_owner_draw_reference's helpers call on its LIB.
    PD = C.POINTER(tod.Draw)
    lib.em_owner_draw_001CA7B0.argtypes = [PD, P32, C.c_uint32, C.POINTER(C.c_int32)]
    lib.em_owner_draw_001CA940.argtypes = [PD, C.c_int32, C.POINTER(osr.Model)]
    lib.em_world_models_parse.argtypes = [C.POINTER(tod.WorldModels), C.c_void_p, C.c_size_t]
    lib.em_world_models_001C6120.argtypes = [C.POINTER(tod.WorldModels), C.c_uint32, C.c_uint32, P32]
    lib.em_owner_services_001CAA00.argtypes = [C.POINTER(Services), C.POINTER(Owner)]
    lib.em_load_veil_particles_001D1F80.argtypes = [C.POINTER(tod.Veil), C.c_int32, C.c_int32, C.c_int32]
    return lib


def u32(b, a): return struct.unpack_from('<I', b, a)[0]
def words(data): return (C.c_uint32 * (len(data) // 4)).from_buffer_copy(bytes(data))
def wbytes(arr): return bytes(arr)


class State:
    """One set of the bytes 001D89D0 reads and writes, as native views."""

    def __init__(self, *, mode, ctx0c, ctx0220, ctx2380, area, table, seed, view, rig, rigptr,
                 cls, kind, pose, rgb, pos, radius, node_rows, a, b, arg3):
        self.mode = (C.c_int32 * 1)(mode)
        self.ctx0c, self.lights, self.tmpl = words(ctx0c), words(ctx0220), words(ctx2380)
        self.area = (C.c_uint8 * 2)(*area)
        self.table, self.seed, self.view = words(table), words(seed), words(view)
        self.rig, self.rigptr = words(rig), words(rigptr)
        self.a, self.b, self.arg3 = words(a), words(b), words(arg3)
        self.light = Light()
        w = self.light.world
        w.d00275688, w.d00817BC0 = C.cast(self.rigptr, P32), C.cast(self.rig, P32)
        w.ctx_246C = C.cast(self.mode, C.POINTER(C.c_int32))
        w.ctx_000C, w.ctx_0220, w.ctx_2380 = (C.cast(x, P32) for x in (self.ctx0c, self.lights, self.tmpl))
        w.d00810700 = C.cast(self.area, C.POINTER(C.c_uint8))
        w.d00251C50, w.d00253170, w.d00810610 = (C.cast(x, P32) for x in (self.table, self.seed, self.view))
        o = self.owner = LightOwner()
        o.cls, o.kind, o.pose_bone = cls, kind, pose
        o.rgb[:], o.pos[:] = struct.unpack('<4I', rgb), struct.unpack('<4I', pos)
        self.radius = (C.c_uint32 * 1)(radius) if radius is not None else None
        o.model_radius = C.cast(self.radius, P32) if self.radius is not None else None
        self.rows = [words(r) if r is not None else None for r in node_rows]
        self.slots = (P32 * max(1, len(node_rows)))(*[C.cast(r, P32) if r is not None else None
                                                      for r in self.rows])
        o.node_c0, o.node_count = self.slots, len(node_rows)

    def run(self):
        rc = LIB.em_actor_light_001D89D0(C.byref(self.light), C.byref(self.owner), self.a, self.b, self.arg3)
        return rc, self.light.fault.address, self.light.fault.code


# ---------------------------------------------------------------- A. synthetic

def rf(rng, lo, hi): return bits(number(bits(rng.uniform(lo, hi))))


def lane(rng, lo, hi, p):
    return rng.choice(SPECIALS) if rng.random() < p else rf(rng, lo, hi)


def vec(rng, lo, hi, p, n=4): return [lane(rng, lo, hi, p) for _ in range(n)]


def pack(ws): return struct.pack(f'<{len(ws)}I', *[w & M32 for w in ws])


MODES = [0] * 10 + [2, 2, 1, 3, 4, 5, 6, 7, 8, -1, -0x80000000, 100]


def synthetic_inputs(seed):
    rng = random.Random(0x89D0 ^ seed * 0x9E3779B1)
    p = 0.0 if seed % 3 else 0.06
    mode = MODES[seed % len(MODES)]
    cls = rng.getrandbits(8) & ~0x60 | (0x20 if seed & 1 else 0) | (0x40 if seed % 5 == 0 else 0)
    kind = rng.choice(list(EXCLUDED) + [0x06, 0x0E, 0x14, 0x11, rng.getrandbits(8), rng.getrandbits(8)])
    radius = rng.choice([rf(rng, 0, 60), rf(rng, 0, 29.9), bits(30.0), bits(30.0) - 1, bits(30.0) + 1,
                         0x80000000, 0x7F800000, 0x7FC00000, bits(-3.0), 0])
    pose = 0xFF if seed % 4 < 2 else rng.randrange(4)
    point = vec(rng, -300, 300, p)
    if seed % 7 == 0: point[3] = ONE
    node_rows = [pack(vec(rng, -300, 300, p)) for _ in range(4)]
    if pose != 0xFF: point = list(struct.unpack('<4I', node_rows[pose]))
    pos = pack(point) if pose == 0xFF else pack(vec(rng, -300, 300, p))
    rgb = pack([lane(rng, 0, 8, p), lane(rng, 0, 8, p), lane(rng, 0, 8, p),
                rng.choice([ONE, bits(1.5), bits(0.5), bits(2.0), lane(rng, -2, 4, p)])])
    arg3 = rgb if seed % 6 else pack([lane(rng, 0, 4, p) for _ in range(3)] + [rf(rng, 0, 3)])
    if seed % 3 == 1:   # wide magnitudes: the bias adds and products round differently by order
        rgb = pack([bits(rng.choice([-1, 1]) * rng.uniform(1, 2) * 2.0 ** rng.randint(-30, 30))
                    for _ in range(4)])
        arg3 = rgb if seed % 2 else pack([bits(rng.uniform(-3e7, 3e7)) for _ in range(4)])
    # context +0x0C: bit 8 selects the 0x0F00 key.
    ctx0c = pack([rng.getrandbits(32) & ~0x100 | (0x100 if seed % 10 == 3 else 0)])
    area = (rng.choice([11, 1, 2, 0x0F]), rng.choice([0, 1, 2]))
    key = area[0] << 8 | area[1]
    keys = []
    for _ in range(45):
        k = rng.getrandbits(16)
        keys.append(k | 0x10000 if k in (key, 0xF00) else k)
    slots = rng.sample(range(45), 3)
    hit = seed % 8
    if hit in (0, 3, 4): keys[slots[0]] = key                        # one hit
    if hit == 1: keys[slots[0]] = keys[slots[1]] = key               # two: the first wins
    if hit in (2, 5): keys[slots[2]] = 0xF00                         # the 0x0F00 key present
    if hit == 2: keys[slots[0]] = key
    table = bytearray()
    for k in keys:
        ang = lambda: lane(rng, -6.3, 6.3, p)
        col = lambda: lane(rng, 0, 2, p)
        entry = [k] + [rng.getrandbits(32) for _ in range(7)] + [ang(), ang()] + \
                [col(), col(), col(), lane(rng, -1, 3, p)] + [ang(), ang()] + [col() for _ in range(4)] + \
                [ang(), ang()] + [col() for _ in range(4)] + [col() for _ in range(3)] + [rng.getrandbits(32)]
        assert len(entry) == 30
        table += pack(entry)
    lights = bytearray()
    for j in range(32):
        w_choice = rng.random()
        weight = (rf(rng, 0.01, 60) if w_choice < 0.45 else 0 if w_choice < 0.6 else
                  rf(rng, -30, -0.01) if w_choice < 0.72 else rng.choice([0x80000000, 1, 0x80000001, 0x7F800000,
                                                                           0x7FC00000, rf(rng, 0, 1e-3)]))
        near = rng.random() < 0.3
        base = [number(point[k]) if (point[k] >> 23 & 255) != 255 else 0.0 for k in range(3)]
        epos = [bits(base[k] + rng.uniform(-0.6, 0.6) if near else base[k] + rng.uniform(-40, 40))
                for k in range(3)] + [rng.choice([ONE, 0, rf(rng, -1, 1)])]
        epos = [x if rng.random() > p else rng.choice(SPECIALS) for x in epos]
        ent = [rng.getrandbits(32) for _ in range(4)] + epos + vec(rng, -3, 3, p) + \
              [rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32), weight] + vec(rng, -1.2, 1.2, p, 16)
        lights += pack(ent)
    ctx2380 = pack(vec(rng, -5, 5, p, 16))
    seedv = pack([0, 0, 0, 0] if seed % 4 else vec(rng, -1, 1, p))
    view = pack(vec(rng, -1, 1, p, 12) + vec(rng, -500, 500, p, 3) + [ONE])
    rig = pack(vec(rng, -6.3, 6.3, p, RIG_SIZE // 4))
    rigptr = pack([rng.getrandbits(32)])
    a = pack([rng.getrandbits(32) for _ in range(16)])
    b = pack([rng.getrandbits(32) for _ in range(16)])
    return dict(mode=mode, ctx0c=ctx0c, ctx0220=bytes(lights), ctx2380=ctx2380, area=area,
                table=bytes(table), seed=seedv, view=view, rig=rig, rigptr=rigptr, cls=cls, kind=kind,
                pose=pose, rgb=rgb, pos=pos, radius=radius, node_rows=node_rows, a=a, b=b, arg3=arg3)


def run_original_synthetic(x):
    o = EE(ELF)
    o.put32(CTX_PTR, CONTEXT)
    o.put(CONTEXT + 0x0C, x['ctx0c'])
    o.put(CONTEXT + 0x220, x['ctx0220'])
    o.put(CONTEXT + 0x2380, x['ctx2380'])
    o.put32(CONTEXT + 0x246C, x['mode'])
    o.put(AREA, bytes(x['area']))
    o.put(TABLE, x['table'])
    o.put(SEED, x['seed'])
    o.put(VIEW, x['view'])
    o.put(RIG, x['rig'])
    o.put(RIG_PTR, x['rigptr'])
    o.put(OWNER + 2, bytes([x['cls'], x['kind']]))
    o.put32(OWNER + 0x44, MODEL)
    o.put32(MODEL + 0x20, x['radius'])
    o.put(OWNER + 0x80, x['rgb'])
    o.put(OWNER + 0x98, bytes([x['pose']]))
    o.put(OWNER + 0xB0, x['pos'])
    for k, row in enumerate(x['node_rows']):
        o.put32(OWNER + 0x110 + 4 * k, NODES + 0xD0 * k)
        o.put(NODES + 0xD0 * k + 0xC0, row)
    arg3 = OWNER + 0x80 if x['arg3'] == x['rgb'] else ARG3
    o.put(ARG3, x['arg3'])
    o.put(A_SPR, x['a'])
    o.put(B_SPR, x['b'])
    o.run(ENTRY, (OWNER, A_SPR, B_SPR, arg3))
    allowed = [(A_SPR, A_SPR + 0x80), (RIG, RIG + RIG_SIZE), (RIG_PTR, RIG_PTR + 4)]
    extra = [a for a in o.written if not any(lo <= a < hi for lo, hi in allowed)]
    assert not extra, ('001D89D0 wrote outside the modelled bytes', [hex(a) for a in sorted(extra)[:4]])
    return dict(a=o.read(A_SPR, 64), b=o.read(B_SPR, 64), rig=o.read(RIG, RIG_SIZE),
                rigptr=o.read(RIG_PTR, 4))


def path_of(x):
    mode = x['mode']
    if mode in (1, 3, 4, 5, 6): return f'mode {mode}'
    gate = x['kind'] not in EXCLUDED and fm_lt(x['radius'], bits(30.0))
    return 'rig' + (' fill' if x['cls'] & 0x20 else '') + (' glow' if x['cls'] & 0x40 else '') + \
           (' fold' if gate else '') + (' node' if x['pose'] != 0xFF else '')


def fm_lt(a, b):
    import ee_float_model as fm
    return bool(fm.ee_c_lt(a, b))


def run_case(x, label):
    want = run_original_synthetic(x)
    s = State(**x)
    rc, where, code = s.run()
    assert rc == 0 and code == 0, ('native fault', label, hex(where), code)
    got = dict(a=wbytes(s.a), b=wbytes(s.b), rig=wbytes(s.rig), rigptr=wbytes(s.rigptr))
    for k in ('a', 'b', 'rig', 'rigptr'):
        assert got[k] == want[k], ('001D89D0', label, path_of(x), k)
    return path_of(x)


def case_synthetic(seed):
    return run_case(synthetic_inputs(seed), seed)


def boundary_inputs():
    """Hand-built states around the fold's compares. The camera fill with an
    identity view and zero angles gives the slot 0 direction (0, 1, 0, 1);
    a negative slot 0 weight and a -0 seed leave the fold accumulator with
    -0 lanes, which only a skipped light keeps: every light is negative
    (skipped) except one whose weight is +0, -0, a denormal, the smallest
    normal or 1.0, placed at squared distance 1.0 (the clamp boundary),
    0.25 or 4.0."""
    out = []
    ident = [ONE, 0, 0, 0, 0, ONE, 0, 0, 0, 0, ONE, 0, 0, 0, 0, ONE]
    for n, (weight, dist) in enumerate((w, d) for w in (0, 0x80000000, 1, 0x80000001, 0x00800000, ONE)
                                       for d in (1.0, 0.5, 2.0)):
        x = synthetic_inputs(7 + 8 * n)
        x.update(mode=0, cls=0x20, kind=0x06, radius=bits(5.0), pose=0xFF, view=pack(ident),
                 seed=pack([0x80000000] * 4), ctx0c=pack([0]), area=(11, 0))
        point = [bits(10.0), bits(-4.0), bits(7.0), ONE]
        x['pos'] = pack(point)
        entry = [11 << 8] + [0] * 7 + [0, 0] + [bits(0.5), bits(0.25), bits(0.125), bits(-2.0)] + \
                [bits(0.3), bits(-0.2)] + [bits(0.4)] * 4 + [bits(1.1), bits(0.7)] + [bits(0.2)] * 4 + \
                [bits(0.1)] * 3 + [0]
        x['table'] = pack(entry) + x['table'][0x78:]
        rig = bytearray(x['rig']); rig[0x88:0x8C] = bytes(4); x['rig'] = bytes(rig)
        lights = bytearray(x['ctx0220'])
        for j in range(32):
            struct.pack_into('<I', lights, 0x80 * j + 0x2C, bits(-1.0))
        struct.pack_into('<4I', lights, 0x80 * 5 + 0x10, bits(10.0 + dist), point[1], point[2], ONE)
        struct.pack_into('<I', lights, 0x80 * 5 + 0x2C, weight)
        x['ctx0220'] = bytes(lights)
        out.append(x)
    return out


def case_boundary(index):
    return run_case(BOUNDARY[index], f'boundary {index}')


def case_binding(seed):
    """em_actor_light_w_001D89D0 over an EmOwnerServicesOwner built from a
    synthetic state (arg3 = owner + 0x80, as 001C7420 passes it): A, B, the
    rig record and D_00275688 must equal the original's."""
    x = synthetic_inputs(seed)
    x['arg3'] = x['rgb']
    want = run_original_synthetic(x)
    s = State(**x)
    ow, model = Owner(), osr.Model()
    ow.cls, ow.kind, ow.pose_bone = x['cls'], x['kind'], x['pose']
    C.memmove(C.addressof(ow.pos), x['pos'], 16)
    C.memmove(C.addressof(model) + osr.Model.radius.offset, struct.pack('<I', x['radius']), 4)
    ow.model = C.pointer(model)
    bones = (Bone * 4)()
    for k, row in enumerate(x['node_rows']):
        C.memmove(C.addressof(bones[k].world) + 48, row, 16)
        ow.bone[k] = C.pointer(bones[k])
    binding = Binding()
    binding.light = C.pointer(s.light)

    def rgb(_, owner_view, out):
        for k, w in enumerate(struct.unpack('<4I', x['rgb'])): out[k] = w
        return 0

    cb = RGB_FN(rgb)
    binding.w_owner_rgb = cb
    fa = (C.c_float * 16).from_buffer_copy(x['a'])
    fb = (C.c_float * 16).from_buffer_copy(x['b'])
    rc = LIB.em_actor_light_w_001D89D0(C.byref(binding), C.byref(ow), fa, fb)
    assert rc == 0 and s.light.fault.code == 0, ('binding fault', seed, hex(s.light.fault.address))
    for k, got in (('a', bytes(fa)), ('b', bytes(fb)), ('rig', wbytes(s.rig)), ('rigptr', wbytes(s.rigptr))):
        assert got == want[k], ('binding', seed, path_of(x), k)
    return path_of(x)


def case_8c30(item):
    """001D8C30 alone, every mode including those 001D89D0 never passes."""
    mode, seed = item
    rng = random.Random(0x8C30 ^ seed)
    vin = [bits(rng.choice([-1, 1]) * rng.uniform(1, 2) * 2.0 ** rng.randint(-30, 30)) for _ in range(4)]
    if seed % 3 == 0: vin[3] = rng.choice([ONE, bits(0.5), 0x80000000, bits(1.0000001), 0x7F800000])
    if seed % 4 == 1:   # the edges of the exponent range: x 128 saturates or leaves the denormals
        vin = [rng.choice([bits(1.3 * 2.0 ** 125), bits(-1.7 * 2.0 ** 124), bits(1.7 * 2.0 ** -121),
                           bits(-1.1 * 2.0 ** -126), 0x7F7FFFFF, 0x00800001]) for _ in range(4)]
    a = pack([rng.getrandbits(32) for _ in range(16)])
    b = pack([rng.getrandbits(32) for _ in range(16)])
    tmpl = pack([rng.getrandbits(32) for _ in range(16)])
    o = EE(ELF)
    o.put32(CTX_PTR, CONTEXT)
    o.put(CONTEXT + 0x2380, tmpl)
    o.put(ARG3, pack(vin))
    o.put(A_SPR, a)
    o.put(B_SPR, b)
    o.run(0x1D8C30, (mode & M64 if mode >= 0 else (mode + (1 << 64)), A_SPR, B_SPR, ARG3))
    extra = [x for x in o.written if not A_SPR <= x < A_SPR + 0x80]
    assert not extra, ('001D8C30 wrote outside A and B', mode)
    light = Light()
    t = words(tmpl)
    light.world.ctx_2380 = C.cast(t, P32)
    na, nb, ni = words(a), words(b), words(pack(vin))
    assert LIB.em_actor_light_001D8C30(C.byref(light), mode, na, nb, ni) == 0
    assert wbytes(na) == o.read(A_SPR, 64) and wbytes(nb) == o.read(B_SPR, 64), ('001D8C30', mode, seed)
    return mode


# ---------------------------------------------------------------- B/C. captures

CAP = {}


def owner_inputs(ram, spr, owner, mode=0):
    ctx = u32(ram, CTX_PTR)
    model = u32(ram, owner + 0x44)
    pose = ram[owner + 0x98]
    rows = [None] * MAX_BONES
    if pose != 0xFF:
        node = u32(ram, owner + 0x110 + 4 * pose)
        rows[pose] = ram[node + 0xC0:node + 0xD0] if node else None
    return dict(mode=mode, ctx0c=ram[ctx + 0x0C:ctx + 0x10], ctx0220=ram[ctx + 0x220:ctx + 0x1220],
                ctx2380=ram[ctx + 0x2380:ctx + 0x23C0], area=(ram[AREA], ram[AREA + 1]),
                table=ram[TABLE:TABLE + TABLE_SIZE], seed=ram[SEED:SEED + 16], view=ram[VIEW:VIEW + 64],
                rig=ram[RIG:RIG + RIG_SIZE], rigptr=ram[RIG_PTR:RIG_PTR + 4], cls=ram[owner + 2],
                kind=ram[owner + 3], pose=pose, rgb=ram[owner + 0x80:owner + 0x90],
                pos=ram[owner + 0xB0:owner + 0xC0], radius=u32(ram, model + 0x20) if model else None,
                node_rows=rows, a=spr[0x3400:0x3440], b=spr[0x3440:0x3480], arg3=ram[owner + 0x80:owner + 0x90])


def case_capture_light(item):
    """B: the original 001D89D0 after 001D8C20(0), as 001CA990 reaches it."""
    name, owner = item
    ram, spr = CAP[name]
    o = EE(ELF, ram, spr)
    ctx = u32(ram, CTX_PTR)
    for a, v in ((0x275B48, owner), (0x275B44, owner), (0x275B40, owner + 0x110), (ctx + 0x246C, 0)):
        o.put32(a, v)
    o.run(ENTRY, (owner, A_SPR, B_SPR, owner + 0x80))
    allowed = [(A_SPR, A_SPR + 0x80), (RIG, RIG + RIG_SIZE), (RIG_PTR, RIG_PTR + 4)]
    extra = [a for a in o.written if not any(lo <= a < hi for lo, hi in allowed)]
    assert not extra, ('001D89D0 wrote outside the modelled bytes', name, [hex(a) for a in sorted(extra)[:4]])
    x = owner_inputs(ram, spr, owner)
    s = State(**x)
    rc, where, code = s.run()
    assert rc == 0 and code == 0, ('native fault', name, hex(owner), hex(where), code)
    for k, addr, n, arr in (('a', A_SPR, 64, s.a), ('b', B_SPR, 64, s.b), ('rig', RIG, RIG_SIZE, s.rig),
                            ('rigptr', RIG_PTR, 4, s.rigptr)):
        assert wbytes(arr) == o.read(addr, n), ('captured 001D89D0', name, hex(owner), k)
    gate = x['kind'] not in EXCLUDED and x['radius'] is not None and fm_lt(x['radius'], bits(30.0))
    lit = sum(1 for j in range(32) if not fm_le(u32(x['ctx0220'], 0x80 * j + 0x2C), 0))
    return dict(fold=gate, lights=lit if gate else 0, fill=bool(x['cls'] & 0x20), glow=bool(x['cls'] & 0x40),
                node=x['pose'] != 0xFF)


def fm_le(a, b):
    import ee_float_model as fm
    return bool(fm.ee_c_le(a, b))


def case_chain(item):
    """C: the native owner draw chain with this module bound as w_001D89D0."""
    name, owner = item
    ram, spr = CAP[name]
    behaviour, count = u32(ram, owner + 0x10), ram[owner + 0x0C]
    o, ctx = tod.original_draw(ram, spr, owner)
    used = o.load(ctx + 0x10) - tod.CAP_DL
    unit = o.read(tod.CAP_DL, used)
    hits = tod.captured_unit(ram, unit) if used else []

    bones = (Bone * MAX_BONES)()
    bone_ptr = (C.POINTER(Bone) * MAX_BONES)()
    ow = Owner()
    for field, off, size in OWNER_BYTES:
        setattr(ow, field, int.from_bytes(ram[owner + off:owner + off + size], 'little'))
    for field, off, n in OWNER_FLOATS:
        C.memmove(C.addressof(getattr(ow, field)), ram[owner + off:owner + off + 4 * n], 4 * n)
    handle = C.c_uint32(0)
    assert LIB.em_world_models_001C6120(C.byref(tod.BANK), u32(ram, 0x28A59C), ram[owner + 0x0D],
                                        C.byref(handle)) == 0
    assert handle.value == u32(ram, owner + 0x44), ('bank handle', hex(handle.value))
    entry = next(i for i in range(tod.BANK.model_count) if tod.BANK.models[i].address == handle.value)
    ow.model = C.pointer(tod.BANK.models[entry].model)
    for i in range(count):
        node = u32(ram, owner + 0x110 + 4 * i)
        b = bones[i]
        C.memmove(C.addressof(b.bind), ram[node:node + 64], 64)
        b.parent = struct.unpack_from('<h', ram, node + 0x64)[0]
        C.memmove(C.addressof(b.rot), ram[node + 0x70:node + 0x7C], 12)
        C.memmove(C.addressof(b.trans), ram[node + 0x7C:node + 0x88], 12)
        C.memmove(C.addressof(b.scale), ram[node + 0x88:node + 0x8E], 6)
        C.memmove(C.addressof(b.world), ram[node + 0x90:node + 0xD0], 64)
        bone_ptr[i] = C.pointer(b)
        ow.bone[i] = bone_ptr[i]
    fill = bytes((i * 29 + 7) & 0xFF for i in range(tod.CAP_DL_SIZE))
    buf = (C.c_uint8 * tod.CAP_DL_SIZE).from_buffer_copy(fill)
    ch = (Channel * 1)()
    ch[0].cursor, ch[0].end = C.addressof(buf), C.addressof(buf) + tod.CAP_DL_SIZE
    scratch = Scratch()
    for field, off in (('s3400', 0x3400), ('s3440', 0x3440), ('s3480', 0x3480), ('s3AC0', 0x3AC0)):
        C.memmove(C.addressof(getattr(scratch, field)), spr[off:off + 64], 64)
    view = words(ram[VIEW:VIEW + 64])
    planes = words(ram[ctx + 0x2410:ctx + 0x2450])
    c0c, c9c = words(ram[ctx + 0x0C:ctx + 0x10]), words(ram[ctx + 0x9C:ctx + 0xA0])
    arena, c50 = words(ram[0x275674:0x275678]), words(ram[ctx + 0x50:ctx + 0x54])
    d = tod.Draw()
    d.world.d00810610, d.world.ctx_2410 = C.cast(view, P32), C.cast(planes, P32)
    d.world.ctx_0C, d.world.ctx_9C, d.world.d00275674 = C.cast(c0c, P32), C.cast(c9c, P32), C.cast(arena, P32)
    d.world.channel, d.world.channel_count = C.cast(ch, C.POINTER(Channel)), 1
    d.world.ctx_50, d.world.ctx_50_count = C.cast(c50, P32), 1
    d.world.models = C.pointer(tod.BANK)
    veil = tod.Veil()
    vcur = words(bytes(4))
    veil.world.cursor, veil.world.cursor_count = C.cast(vcur, P32), 1
    veil.world.d00275674 = C.cast(arena, P32)
    veil.world.packet, veil.world.packet_address, veil.world.packet_size = \
        C.addressof(buf), tod.CAP_DL, tod.CAP_DL_SIZE
    # This module's storage: the canonical views, the lighting mode word
    # shared with the 001D8C20 worker (starting at the captured value).
    light_state = State(**owner_inputs(ram, spr, owner, mode=struct.unpack_from('<i', ram, ctx + 0x246C)[0]))
    light = light_state.light
    calls = []

    def w_rgb(_, owner_view, out):
        calls.append('rgb')
        for k in range(4): out[k] = u32(ram, owner + 0x80 + 4 * k)
        return 0

    binding = Binding()
    binding.light = C.pointer(light)
    rgb_cb = RGB_FN(w_rgb)
    binding.w_owner_rgb = rgb_cb

    def w_a7b0(_, pos, radius, out):
        p = C.cast(pos, P32)
        return LIB.em_owner_draw_001CA7B0(C.byref(d), (C.c_uint32 * 4)(p[0], p[1], p[2], 0), radius, out)

    def w_8c20(_, mode):
        calls.append('8c20')
        light_state.mode[0] = mode
        return 0

    def w_1f80(_, a0, a1, a2):
        vcur[0] = tod.CAP_DL + (ch[0].cursor - C.addressof(buf))
        rc = LIB.em_load_veil_particles_001D1F80(C.byref(veil), a0, a1, a2)
        ch[0].cursor = C.addressof(buf) + (vcur[0] - tod.CAP_DL)
        return rc

    def w_a940(_, flags, model):
        return LIB.em_owner_draw_001CA940(C.byref(d), flags, model)

    def refuse(*_):
        calls.append('b3c0')
        return -1

    svc = Services()
    svc.world.d00275B40, svc.world.d00275B40_count = C.cast(bone_ptr, C.POINTER(C.POINTER(Bone))), count
    svc.world.scratch = C.pointer(scratch)
    svc.world.channel, svc.world.channel_count = C.cast(ch, C.POINTER(Channel)), 1
    keep = [rgb_cb]
    for field, fn in (('w_001CA7B0', w_a7b0), ('w_001D8C20', w_8c20), ('w_001D1F80', w_1f80),
                      ('w_001CA940', w_a940), ('w_001CB3C0', refuse)):
        cf = WORKER_TYPES[field](fn)
        keep.append(cf)
        setattr(svc.workers, field, cf)
    # The bound translation itself: its C entry point, not a Python stand-in.
    svc.workers.w_001D89D0 = C.cast(LIB.em_actor_light_w_001D89D0, WORKER_TYPES['w_001D89D0'])
    svc.workers.ctx = C.cast(C.pointer(binding), C.c_void_p)
    rc = LIB.em_owner_services_001CAA00(C.byref(svc), C.byref(ow))
    assert rc == 0 and svc.fault.code == 0 and d.fault.code == 0 and veil.fault.code == 0 and \
        light.fault.code == 0, ('native fault', name, hex(owner), hex(svc.fault.address), svc.fault.code,
                                hex(light.fault.address), light.fault.code, calls)
    native_used = ch[0].cursor - C.addressof(buf)
    assert native_used == used, ('cursor', name, hex(owner), native_used, used)
    assert bytes(buf) == o.read(tod.CAP_DL, tod.CAP_DL_SIZE), ('native unit != original', name, hex(owner))
    if not used:
        assert calls == [], ('culled owner reached lighting', name, hex(owner), calls)
        return dict(capture=name, owner=hex(owner), behaviour=hex(behaviour), unit=0, in_list=False)
    assert calls == ['8c20', 'rgb'], ('worker calls', calls)
    assert light_state.mode[0] == 0 and o.load(ctx + 0x246C) == 0
    assert bytes(scratch.s3400) == o.read(A_SPR, 64), ('A', name, hex(owner))
    assert bytes(scratch.s3440) == o.read(B_SPR, 64), ('SPR 0x70003440', name, hex(owner))
    # B: 001C7420 packs the worker's B into the colour CNT payload; the native
    # unit equals the original's (above), so B equals the unit's payload.
    assert bytes(buf)[0x20:0x60] == unit[0x20:0x60], ('B', name, hex(owner))
    assert wbytes(light_state.rig) == o.read(RIG, RIG_SIZE), ('rig record', name, hex(owner))
    assert wbytes(light_state.rigptr) == o.read(RIG_PTR, 4), ('D_00275688', name, hex(owner))
    return dict(capture=name, owner=hex(owner), behaviour=hex(behaviour), unit=used, in_list=bool(hits))


# ---------------------------------------------------------------- D. faults

def fault_contract():
    x = synthetic_inputs(1)
    x.update(mode=0, cls=0x20, kind=0x06, radius=bits(5.0), pose=0xFF)
    checks = 0

    def expect(state, address, code, untouched=True):
        nonlocal checks
        before = [wbytes(v) for v in (state.a, state.b, state.rig, state.rigptr)]
        rc, where, got = state.run()
        assert rc == -1 and (where, got) == (address, code), ('fault', hex(where), got, hex(address), code)
        if untouched:
            assert [wbytes(v) for v in (state.a, state.b, state.rig, state.rigptr)] == before, 'partial write'
        # latched: a later call returns -1 and keeps the first fault
        assert state.run() == (-1, address, code)
        checks += 1

    for field, address in (('ctx_246C', 0x1D89D0), ('d00275688', 0x1D89D0), ('d00817BC0', 0x1D89D0),
                           ('ctx_000C', 0x1D2710), ('d00810700', 0x1D7B30), ('d00251C50', 0x1D7B30),
                           ('d00810610', 0x1D8340), ('d00253170', 0x1D8340), ('ctx_0220', 0x1D8340)):
        s = State(**x)
        setattr(s.light.world, field, None)
        expect(s, address, 1)
    s = State(**x); s.owner.model_radius = None
    expect(s, 0x1D8270, 1)
    s = State(**dict(x, kind=0x03)); s.owner.model_radius = None; s.light.world.d00253170 = None
    rc, where, code = s.run()
    assert rc == 0 and code == 0, 'excluded type must not read the model or the fold views'
    checks += 1
    s = State(**dict(x, pose=4))
    expect(s, 0x1D89D0, 4)
    rows = list(x['node_rows']); rows[2] = None
    s = State(**dict(x, pose=2, node_rows=rows))
    expect(s, 0x1D8340, 1)
    s = State(**dict(x, pose=2, node_rows=rows, kind=0x09))  # gate closed: the node is never read
    assert s.run()[0] == 0
    checks += 1
    s = State(**dict(x, mode=4)); s.light.world.ctx_2380 = None
    expect(s, 0x1D8C30, 1)
    s = State(**dict(x, mode=3)); s.light.world.ctx_2380 = None; s.light.world.d00817BC0 = None
    assert s.run()[0] == 0, 'mode 3 reads neither the template nor the rig'
    checks += 1
    s = State(**x)
    assert LIB.em_actor_light_001D89D0(C.byref(s.light), None, s.a, s.b, s.arg3) == -1
    assert (s.light.fault.address, s.light.fault.code) == (0x1D89D0, 1)
    checks += 1
    s = State(**dict(x, mode=5))
    assert LIB.em_actor_light_001D89D0(C.byref(s.light), None, s.a, s.b, s.arg3) == 0, \
        'modes 1, 3..6 never read the owner'
    checks += 1
    # the binding: a missing rgb worker, a failing one, no model
    binding = Binding(); s = State(**x); binding.light = C.pointer(s.light)
    ow = Owner()
    fa, fb = (C.c_float * 16)(), (C.c_float * 16)()
    assert LIB.em_actor_light_w_001D89D0(C.byref(binding), C.byref(ow), fa, fb) == -1
    assert (s.light.fault.address, s.light.fault.code) == (0x1D89D0, 1)
    s = State(**x); binding.light = C.pointer(s.light)
    bad = RGB_FN(lambda *_: -1); binding.w_owner_rgb = bad
    assert LIB.em_actor_light_w_001D89D0(C.byref(binding), C.byref(ow), fa, fb) == -1
    assert (s.light.fault.address, s.light.fault.code) == (0x1D89D0, 2)
    s = State(**x); binding.light = C.pointer(s.light)
    good = RGB_FN(lambda _c, _o, out: 0); binding.w_owner_rgb = good
    ow.kind, ow.pose_bone = 0x06, 0xFF
    assert LIB.em_actor_light_w_001D89D0(C.byref(binding), C.byref(ow), fa, fb) == -1
    assert (s.light.fault.address, s.light.fault.code) == (0x1D8270, 1)
    checks += 3
    return checks


# ---------------------------------------------------------------- main

LIB = ELF = BOUNDARY = None


def main():
    global LIB, ELF
    started = time.time()
    ELF = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(ELF).hexdigest() == ELF_SHA, 'not the pinned SCUS-97112 ELF'
    LIB = build_library()
    tod.ELF, tod.LIB, tod.CAP = ELF, LIB, CAP
    counts = {}

    # The oracle's MMI extension must reproduce 00102798 as a transpose.
    o = EE(ELF)
    m = list(range(1, 17))
    o.put(0x500000, struct.pack('<16I', *m))
    o.run(0x102798, (0x500100, 0x500000))
    assert list(struct.unpack('<16I', o.read(0x500100, 64))) == [m[4 * c + r] for r in range(4) for c in range(4)]

    base = DECOMP / 'build/s87/route'
    for beat in tod.BEATS:
        p = base / beat
        if (p / 'eeMemory.bin').exists() and (p / 'scratchpad.bin').exists():
            ram = (p / 'eeMemory.bin').read_bytes()
            assert ram[AREA] == 11, (beat, 'not AREA11')
            for lo, hi in ((0x1D89D0, 0x1D8FD0), (0x1D8130, 0x1D8340 + 0x344), (0x1D7B30, 0x1D7BB0),
                           (0x1D2710, 0x1D2960), (0x102690, 0x102A60), (0x26E520, 0x26E53C)):
                assert ram[lo:hi] == ELF[lo - 0x100000 + 0x300:hi - 0x100000 + 0x300], ('code differs', beat, hex(lo))
            CAP[beat] = (ram, (p / 'scratchpad.bin').read_bytes())
    assert CAP, 'no s87 route captures (build/s87/route/*/eeMemory.bin + scratchpad.bin)'

    # A. synthetic
    seeds = list(range(pick(6000, 440)))
    paths = parallel_map(case_synthetic, seeds)
    counts['synthetic'] = len(paths)
    counts['synthetic_paths'] = {k: paths.count(k) for k in sorted(set(paths))}
    modes = [MODES[k % len(MODES)] for k in seeds]
    counts['synthetic_modes'] = {str(m): modes.count(m) for m in sorted(set(modes))}
    for want in ('mode 1', 'mode 3', 'mode 4', 'mode 5', 'mode 6'):
        assert want in counts['synthetic_paths'], ('path never taken', want)
    for flag in ('fill', 'glow', 'fold', 'node'):
        assert any(flag in k for k in paths), ('path never taken', flag)

    global BOUNDARY
    BOUNDARY = boundary_inputs()
    counts['boundary'] = len(parallel_map(case_boundary, list(range(len(BOUNDARY)))))
    binding_seeds = list(range(pick(1200, 120)))
    counts['binding'] = len(parallel_map(case_binding, binding_seeds))
    items = [(m, k) for m in (0, 1, 2, 3, 4, 5, 6, 7, 8, 100, -1, -0x80000000) for k in range(pick(60, 8))]
    counts['001D8C30_direct'] = len(parallel_map(case_8c30, items))

    # B/C. captures
    tod.load_bank()
    owners = [o for beat in CAP for o in tod.capture_owners(beat)]
    lit = parallel_map(case_capture_light, owners)
    counts['captured_owner_frames'] = len(lit)
    counts['captured_fold'] = sum(r['fold'] for r in lit)
    counts['captured_fold_with_lights'] = sum(1 for r in lit if r['lights'])
    counts['captured_node_point'] = sum(r['node'] for r in lit)
    counts['captured_fill'] = sum(r['fill'] for r in lit)
    counts['captured_glow'] = sum(r['glow'] for r in lit)
    # Every captured owner-frame runs the chain in both modes (about 1 s).
    chain = parallel_map(case_chain, owners)
    counts['chain_owner_frames'] = len(chain)
    counts['chain_units_drawn'] = sum(1 for r in chain if r['unit'])
    counts['chain_units_in_captured_list'] = sum(1 for r in chain if r['in_list'])
    assert counts['chain_units_drawn'] == 119, counts['chain_units_drawn']

    # D. faults
    counts['fault_checks'] = fault_contract()

    line = banner(part(len(seeds), 6000, 'synthetic 001D89D0 states') + f" + {counts['boundary']} boundary",
                  part(len(items), 720, 'direct 001D8C30 cases'),
                  part(len(binding_seeds), 1200, 'binding adapter cases'),
                  f"{len(lit)} captured owner-frames (001D89D0 alone)",
                  f"{len(chain)} owner draw chains" +
                  f" ({counts['chain_units_drawn']} units drawn)",
                  f"{counts['fault_checks']} fault checks")
    report = dict(status='PASS', mode=line, elf_sha256=ELF_SHA, counts=counts,
                  seconds=round(time.time() - started, 1))
    (OUT / 'report.json').write_text(json.dumps(dict(report, chain=chain), indent=1) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    sys.exit(main())
