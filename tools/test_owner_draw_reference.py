#!/usr/bin/env python3
"""Compare the world-owner draw workers (em_owner_draw_original.c) with the
original code, and prove the native owner draw against the captured packets.

The oracle is the owner-services EE interpreter (128-bit GPRs, COP1, VU0
macro mode; float arithmetic = tools/ee_float_model.py). It executes the
ORIGINAL instructions of the pinned SCUS-97112 boot ELF.

A. 001CA7B0 (with 00102948, 001026A0, 00102738): random, special-bit and
   boundary positions, radii, view matrices and cull planes. The returned
   flags must be equal; the original may write nothing but its stack.
B. 001CA940 (001D3C30, 001D38F0, 001D3BA0, 001D38A0, 001D37D0, 001D3AD0,
   vif_append_ref_tag, 001D2910, 001D2710): flags x the context flag bit x
   skin slots x arena words x models / +0x04 words, over a display-list
   window prefilled with a pattern. Every window byte, the cursor and
   context +0x50 must be equal; the original may write nothing else.
C. The world model bank: em_world_models_parse over the exporter's bytes
   (tools/export_world_models.py build(), from the user's extract), each
   model's view against its bytes, and em_world_models_001C6120 against the
   ORIGINAL 001C6120 run over captured RAM for every id and masked variants.
D. Captured draws. For every owner with draw method 001CAA00 and a bank model
   in the s87 route captures 00..14 (crates, drums, fan, truck, elevator,
   husks, panel, door, parachute, ...): the ORIGINAL 001CAA00 runs over the
   captured RAM and scratchpad with the draw loop's current actor set
   (D_00275B48/44 = owner, D_00275B40 = owner + 0x110, as 001CB590 does
   before the +0x10 callback that draws). Its DMA unit must equal the unit in
   the captured display list (all bytes the original writes). Then the NATIVE
   chain runs: em_owner_services_001CAA00 with w_001CA7B0 / w_001CA940 =
   this module (the model bound from the bank through 001C6120),
   w_001D1F80 = em_load_veil_particles_001D1F80, w_001D8C20 recorded, and
   w_001D89D0 answered with the original 001D89D0's A/B (executed on the same
   RAM: no bit-exact native 001D89D0 exists, docs/OWNER_DRAW.md 6). The
   native window (every byte), cursor, context +0x50 and lighting mode must
   equal the original run, and so the captured packets.

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

import ee_float_model as fm
import export_world_models as ewm
import test_owner_services_reference as osr
from reference_mode import FULL, banner, part, pick, select, parallel_map
from test_owner_services_reference import (EE, Owner, Bone, Model, Record, Scratch, Channel, Services,
                                           WORKER_TYPES, OWNER_BYTES, OWNER_FLOATS, MAX_BONES,
                                           M32, ONE, SPR, sx32, bits, number)

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
OUT = ROOT / 'build/owner_draw_reference'

CONTEXT, POS, DL, DL_SIZE, MODEL = 0x600000, 0x700000, 0xA00000, 0x400, 0x1392F40
CAP_DL, CAP_DL_SIZE = 0x1F80000, 0x1000
DRAW = 0x1CAA00
BEATS = ['00_panel_no_battery', '01_battery', '02_elevator_refusal', '03_panel_power', '04_elevator_ride',
         '05_boxes', '06_hill_slide', '07_truck_preview', '08_truck_crossing', '09_fence_door',
         '10_cage_roof_roger', '11_crevice_prompt', '12_crevice_jump', '13_east_tower', '14_roger_encounter']
REQUIRED = {0x1551B0: 'crate', 0x156620: 'drum', 0x827630: 'fan', 0x823FF0: 'truck'}
ALSO = {0x827B10: 'elevator', 0x825940: 'husk creature', 0x827490: 'husk partner'}

P32 = C.POINTER(C.c_uint32)


def s32(v):
    v &= M32
    return v - (1 << 32) if v >> 31 else v


class WorldModel(C.Structure):
    _fields_ = [('model', Model), ('id', C.c_uint32), ('address', C.c_uint32), ('w04', C.c_uint32),
                ('blocks', C.c_uint32), ('size', C.c_uint32), ('bytes', C.c_void_p)]


class WorldModels(C.Structure):
    _fields_ = [('table_address', C.c_uint32), ('count', C.c_uint32), ('span', C.c_void_p),
                ('span_size', C.c_uint32), ('model_count', C.c_uint32), ('models', WorldModel * 64),
                ('records', Record * 512), ('record_count', C.c_uint32)]


class DrawWorld(C.Structure):
    _fields_ = [('d00810610', P32), ('ctx_2410', P32), ('ctx_0C', P32), ('ctx_9C', P32),
                ('d00275674', P32), ('channel', C.POINTER(Channel)), ('channel_count', C.c_uint32),
                ('ctx_50', P32), ('ctx_50_count', C.c_uint32), ('models', C.POINTER(WorldModels))]


class Draw(C.Structure):
    _fields_ = [('world', DrawWorld), ('fault', osr.Fault)]


class VeilWorld(C.Structure):
    _fields_ = [('cursor', P32), ('cursor_count', C.c_uint32), ('ctx_9C', P32), ('d00275674', P32),
                ('d0027568C', P32), ('d0026E880', C.c_void_p), ('d00241010', C.c_void_p),
                ('packet', C.c_void_p), ('packet_address', C.c_uint32), ('packet_size', C.c_uint32),
                ('table', C.c_void_p)]


class VeilWorkers(C.Structure):
    _fields_ = [('ctx', C.c_void_p),
                ('w_0011DF78', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, P32)),
                ('w_001281C0', C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_int32)))]


class Veil(C.Structure):
    _fields_ = [('world', VeilWorld), ('workers', VeilWorkers), ('fault', osr.Fault)]


def build_library():
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / f'owner_draw_{os.getpid()}.dylib' if 'EM_OWNER_DRAW_SOURCE' in os.environ else OUT / 'owner_draw.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-I' + str(ROOT / 'src'),
                    os.environ.get('EM_OWNER_DRAW_SOURCE', str(ROOT / 'src/game/em_owner_draw_original.c')),
                    str(ROOT / 'src/game/em_owner_services_original.c'),
                    str(ROOT / 'src/game/em_load_veil_particles.c'), '-o', str(path)], check=True)
    lib = C.CDLL(str(path))
    PD = C.POINTER(Draw)
    lib.em_owner_draw_001CA7B0.argtypes = [PD, P32, C.c_uint32, C.POINTER(C.c_int32)]
    lib.em_owner_draw_001CA940_at.argtypes = [PD, C.c_int32, C.c_uint32, C.c_uint32]
    lib.em_owner_draw_001CA940.argtypes = [PD, C.c_int32, C.POINTER(Model)]
    lib.em_owner_draw_001CA940_bytes.argtypes = [C.c_int32, C.c_int]
    lib.em_owner_draw_001CA940_bytes.restype = C.c_uint32
    lib.em_world_models_parse.argtypes = [C.POINTER(WorldModels), C.c_void_p, C.c_size_t]
    lib.em_world_models_001C6120.argtypes = [C.POINTER(WorldModels), C.c_uint32, C.c_uint32, P32]
    lib.em_owner_services_001CAA00.argtypes = [C.POINTER(Services), C.POINTER(Owner)]
    lib.em_load_veil_particles_001D1F80.argtypes = [C.POINTER(Veil), C.c_int32, C.c_int32, C.c_int32]
    return lib


def words(*values): return (C.c_uint32 * len(values))(*values)


# ---------------------------------------------------------------- A. 001CA7B0

SPECIALS = [0, 0x80000000, 0x00000001, 0x807FFFFF, 0x00800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000,
            0xFF800000, 0x7FC00000, 0xFFC00000, 0x7F800001, 0xFF800001, ONE, 0xBF800000]


def rf(rng, lo, hi): return bits(number(bits(rng.uniform(lo, hi))))


def lane(rng, lo, hi, p):
    return rng.choice(SPECIALS) if rng.random() < p else rf(rng, lo, hi)


FRUSTUM = [bits(0.4247739315032959), 0, bits(0.9052994847297668), 0,
           bits(-0.4247739315032959), 0, bits(0.9052994847297668), 0,
           0, bits(0.4247739315032959), bits(0.9052994847297668), 0,
           0, bits(-0.4247739315032959), bits(0.9052994847297668), 0]


def a7b0_inputs(seed):
    """Three families: 'frustum' (a translated identity view and the captured
    cull-plane shape: every flag bit and every culling compare occur),
    'captured' (a captured view and planes, positions around captured owners)
    and 'wild' (random lanes with special bit patterns)."""
    rng = random.Random(0xA7B0 ^ seed)
    family = seed % 10
    if family < 4:
        t = [rf(rng, -300, 300), rf(rng, -300, 300), rf(rng, -300, 300)]
        view = [ONE, 0, 0, 0, 0, ONE, 0, 0, 0, 0, ONE, 0] + t + [ONE]
        planes = list(FRUSTUM)
        pos = [bits(number(t[0]) * -1.0 + rng.uniform(-150, 150)), bits(number(t[1]) * -1.0 + rng.uniform(-150, 150)),
               bits(number(t[2]) * -1.0 + rng.uniform(-80, 320)), rng.choice([ONE, 0, 0x7FC00000])]
        pos = [bits(number(x)) for x in pos[:3]] + [pos[3]]
        return view, planes, pos, rf(rng, 0.0, 70.0)
    if family < 7 and CAP:
        ram = CAP[sorted(CAP)[seed % len(CAP)]][0]
        ctx = u32(ram, 0x275670)
        view = list(struct.unpack_from('<16I', ram, 0x810610))
        planes = list(struct.unpack_from('<16I', ram, ctx + 0x2410))
        base = OWNER_POSITIONS[seed % len(OWNER_POSITIONS)]
        pos = [bits(number(base[k]) + rng.uniform(-40, 40)) for k in range(3)] + [ONE]
        return view, planes, pos, rf(rng, 0.0, 60.0)
    p = 0.0 if seed % 4 else 0.08
    view = [lane(rng, -1.0, 1.0, p) for _ in range(12)] + [lane(rng, -600, 600, p) for _ in range(3)] + \
           [lane(rng, 0.9, 1.1, p)]
    planes = []
    for _ in range(4):
        planes += [lane(rng, -1.0, 1.0, p), lane(rng, -1.0, 1.0, p), lane(rng, -1.0, 1.0, p),
                   lane(rng, -5.0, 5.0, p)]
    pos = [lane(rng, -700, 700, p) for _ in range(3)] + [lane(rng, -2, 2, 0.3)]
    radius = lane(rng, 0.0, 80.0, p) if rng.random() < 0.9 else rf(rng, -10.0, 0.0)
    return view, planes, pos, radius


def boundary_inputs():
    """Identity view; all four planes (0, 0, 1, 0) so every compare sees the
    view z: z at -r, r and one ulp either side, zeros and signed zeros."""
    out = []
    ident = [ONE, 0, 0, 0, 0, ONE, 0, 0, 0, 0, ONE, 0, 0, 0, 0, ONE]
    planes = [0, 0, ONE, 0] * 4
    for r in (bits(20.0), bits(24.0), 0, 0x80000000, 0x00000001, bits(-5.0), 0xFF800000, 0x7FC00000):
        for z in (r ^ 0x80000000, r, (r ^ 0x80000000) + 1, (r ^ 0x80000000) - 1 if r & 0x7FFFFFFF else 1,
                  r + 1, r - 1 if r & 0x7FFFFFFF else 0x80000001, 0, 0x80000000):
            out.append((ident, planes, [bits(3.0), bits(-4.0), z & M32, 0], r))
    # Each plane alone decides one bit: plane k = (0, 0, 1, 0), others (0, 0, 0, 0).
    for k in range(4):
        pl = [0] * 16
        pl[4 * k + 2] = ONE
        for z in (bits(-30.0), bits(10.0), bits(30.0)):
            out.append((ident, pl, [0, 0, z, 0], bits(20.0)))
        # The sum order of 00102738: (x + y) + z. With x = 2^24, y = -2^24,
        # z = 1 the other orders lose z (VU truncation), so the bit flips.
        pl = [0] * 16
        pl[4 * k:4 * k + 3] = [ONE, ONE, ONE]
        for p in ([bits(16777216.0), bits(-16777216.0), ONE], [bits(16777216.0), ONE, bits(-16777216.0)],
                  [ONE, bits(16777216.0), bits(-16777216.0)]):
            out.append((ident, pl, p + [0], bits(0.5)))
            out.append((ident, pl, [p[0], p[1], p[2] ^ 0x80000000, 0], bits(0.5)))
    return out


def run_a7b0(view, planes, pos, radius):
    o = EE(ELF)
    o.put32(0x275670, CONTEXT)
    o.put(0x810610, struct.pack('<16I', *view))
    o.put(CONTEXT + 0x2410, struct.pack('<16I', *planes))
    o.put(POS, struct.pack('<4I', *pos))
    o.run(0x1CA7B0, (POS,), (radius,))
    assert not o.written, ('001CA7B0 wrote outside its stack', sorted(o.written)[:4])
    original = s32(o.g(2))
    d = Draw()
    v, pl = words(*view), words(*planes)
    d.world.d00810610 = C.cast(v, P32)
    d.world.ctx_2410 = C.cast(pl, P32)
    flags = C.c_int32(0x5A5A)
    rc = LIB.em_owner_draw_001CA7B0(C.byref(d), words(*pos), radius, C.byref(flags))
    assert rc == 0 and d.fault.code == 0, ('fault', hex(d.fault.address), d.fault.code)
    assert flags.value == original, ('001CA7B0', [hex(x) for x in pos], hex(radius), flags.value, original)
    return original


def case_a7b0(seed):
    return run_a7b0(*a7b0_inputs(seed))


def case_a7b0_fixed(item):
    return run_a7b0(*item)


# ---------------------------------------------------------------- B. 001CA940

def a940_items():
    items = []
    for flags in (0, 1, 2, 3, 0x10, 0x11, 0x1E, 0x1F, -1, -2, 0x7FFFFFFF, -0x80000000 + 1):
        for ctx0c in (0x43, 0x42, 0, 0xFFFFFFFF, 0xFFFFFFFE):
            for slot in (0, 1, 0x1FFFFFF, 0xFFFFFFFF):
                for model, w04 in ((MODEL, 0x30C), (0x13699C0, 0x2698), (0x1000000, 0x12345), (0x7FFFFFF0, 0xFFFFFFFF)):
                    items.append((flags, ctx0c, slot, 0x814220 if slot != 1 else 0xFFFFFFF0, model, w04))
    return items


def case_a940(item):
    flags, ctx0c, slot, arena, model, w04 = item
    rng = random.Random(hash(item) & 0xFFFFFFFF)
    pattern = bytes(rng.getrandbits(8) for _ in range(DL_SIZE))
    o = EE(ELF)
    o.put32(0x275670, CONTEXT)
    o.put32(0x275674, arena)
    o.put32(CONTEXT + 0x0C, ctx0c)
    o.put32(CONTEXT + 0x9C, slot)
    o.put32(CONTEXT + 0x10, DL)
    o.put32(CONTEXT + 0x50, 0xDEADBEEF)
    o.put32(model + 4, w04)
    o.put(DL, pattern)
    o.run(0x1CA940, (sx32(flags), model))
    allowed = [(DL, DL + DL_SIZE), (CONTEXT + 0x10, CONTEXT + 0x14), (CONTEXT + 0x50, CONTEXT + 0x54)]
    extra = [a for a in o.written if not any(lo <= a < hi for lo, hi in allowed)]
    assert not extra, ('001CA940 wrote outside the model', [hex(a) for a in sorted(extra)[:4]])
    buf = (C.c_uint8 * DL_SIZE).from_buffer_copy(pattern)
    ch = (Channel * 1)()
    ch[0].cursor = C.addressof(buf)
    ch[0].end = C.addressof(buf) + DL_SIZE
    c50, c0c, c9c, arena_w = words(0xDEADBEEF), words(ctx0c), words(slot), words(arena)
    d = Draw()
    d.world.ctx_0C, d.world.ctx_9C, d.world.d00275674 = C.cast(c0c, P32), C.cast(c9c, P32), C.cast(arena_w, P32)
    d.world.channel, d.world.channel_count = C.cast(ch, C.POINTER(Channel)), 1
    d.world.ctx_50, d.world.ctx_50_count = C.cast(c50, P32), 1
    rc = LIB.em_owner_draw_001CA940_at(C.byref(d), flags, model, w04)
    assert rc == 0 and d.fault.code == 0, ('fault', hex(d.fault.address), d.fault.code)
    used = o.load(CONTEXT + 0x10) - DL
    assert ch[0].cursor - C.addressof(buf) == used, ('cursor', ch[0].cursor - C.addressof(buf), used)
    assert used == LIB.em_owner_draw_001CA940_bytes(flags, int(not ctx0c & 1)), ('byte count', used)
    assert bytes(buf) == o.read(DL, DL_SIZE), ('window bytes', item)
    assert c50[0] == o.load(CONTEXT + 0x50), ('context +0x50', hex(c50[0]), hex(o.load(CONTEXT + 0x50)))
    return used


# ---------------------------------------------------------------- C. the bank

BANK = BANK_BYTES = BANK_X = None


def load_bank():
    global BANK, BANK_BYTES, BANK_X
    BANK_X = ewm.build(DECOMP / 'extract')
    BANK_BYTES = (C.c_uint8 * 0)()
    data = ewm.serialize(BANK_X, ewm.TABLE_ADDRESS)
    BANK_BYTES = (C.c_uint8 * len(data)).from_buffer_copy(data)
    BANK = WorldModels()
    assert LIB.em_world_models_parse(C.byref(BANK), C.addressof(BANK_BYTES), len(data)) == 0, 'bank parse'
    return data


def check_bank(ram):
    table = ewm.TABLE_ADDRESS
    checked = 0
    for m in BANK_X['models']:
        v = BANK.models[m['id']]
        at = m['offset']
        span = BANK_X['span']
        assert (v.id, v.address, v.w04, v.blocks, v.size) == (m['id'], table + at, m['qwc'], m['blocks'], m['bytes'])
        assert v.model.bone_count == m['bones'] and v.model.skeleton_records == m['bones']
        assert bytes(C.c_uint32.from_buffer(v.model, Model.radius.offset)) == span[at + 0x20:at + 0x24]
        for k in range(m['bones']):
            r = v.model.skeleton[k]
            rec = at + m['skeleton'] + 0x50 * k
            assert r.parent == struct.unpack_from('<h', span, rec + 4)[0]
            assert bytes(r.bind) == span[rec + 0x10:rec + 0x50]
        assert C.string_at(v.bytes, v.size) == span[at:at + m['bytes']]
        for ident in (m['id'], m['id'] | 0x8000, m['id'] | 0x10000, m['id'] | 0xFFFF8000):
            o = EE(ELF, ram)
            o.run(0x1C6120, (table, ident))
            handle = C.c_uint32(0)
            rc = LIB.em_world_models_001C6120(C.byref(BANK), table, ident, C.byref(handle))
            assert rc == 0 and handle.value == o.g(2) & M32, ('001C6120', hex(ident), hex(handle.value))
            checked += 1
    handle = C.c_uint32(0)
    assert LIB.em_world_models_001C6120(C.byref(BANK), table, BANK_X['count'], C.byref(handle)) == -1
    assert LIB.em_world_models_001C6120(C.byref(BANK), table + 0x10, 0, C.byref(handle)) == -1
    return checked


# ---------------------------------------------------------------- D. captures

CAP = {}
OWNER_POSITIONS = []


def u32(b, a): return struct.unpack_from('<I', b, a)[0]


def capture_owners_raw(ram):
    out, a, seen = [], u32(ram, 0x275BC0), set()
    while a and a not in seen:
        seen.add(a)
        if u32(ram, a + 0x4C) == DRAW and u32(ram, a + 0x44):
            out.append((None, a))
        a = u32(ram, a + 0x1C)
    return out


def capture_owners(name):
    ram = CAP[name][0]
    bank = {ewm.TABLE_ADDRESS + m['offset'] for m in BANK_X['models']}
    out, a, seen = [], u32(ram, 0x275BC0), set()
    while a and a not in seen:
        seen.add(a)
        if u32(ram, a + 0x4C) == DRAW and u32(ram, a + 0x44) in bank:
            out.append((name, a))
        a = u32(ram, a + 0x1C)
    return out


def tag_bytes(unit):
    """Offsets of DMA tag bytes +2 and +8..+0xF (never written by the builders;
    the captured list carries whatever an earlier frame left there)."""
    skip, o = set(), 0
    while o < len(unit):
        w0 = u32(unit, o)
        skip.update([o + 2] + list(range(o + 8, o + 16)))
        o += 16 + (16 * (w0 & 0xFFFF) if (w0 >> 28) & 7 in (0, 1) else 0)
    return skip


def tag_offsets(unit):
    """Offsets of the DMA tags of a unit (CNT carries its qwords inline)."""
    out, o = set(), 0
    while o < len(unit):
        out.add(o)
        w0 = u32(unit, o)
        o += 16 + (16 * (w0 & 0xFFFF) if (w0 >> 28) & 7 in (0, 1) else 0)
    return out


def captured_unit(ram, unit):
    """Addresses in RAM whose bytes equal `unit` on every written byte."""
    skip, last, hits, start = tag_bytes(unit), unit[-16:], [], 0
    while True:
        i = ram.find(last[4:8], start)
        if i < 0: return hits
        start = i + 1
        t = i - 4
        if t % 16 or ram[t + 3] != last[3] or ram[t:t + 2] != last[:2]: continue
        p = t - (len(unit) - 16)
        if p < 0: continue
        if all(ram[p + k] == unit[k] for k in range(len(unit)) if k not in skip):
            hits.append(p)


def draw_position(ram, owner):
    """001CAA00's sphere: the position word address and the radius bits."""
    sub = ram[owner + 0x98]
    position = owner + 0xB0 if sub == 0xFF else u32(ram, owner + 0x110 + 4 * sub) + 0xC0
    model = u32(ram, owner + 0x44)
    radius = u32(ram, model + 0x20) if model else 0x41A00000
    if model and fm.ee_c_lt(radius, 0x41A00000):
        radius = fm.ee_mul(radius, 0x3F99999A)
    return position, radius


def flag_class(item):
    name, owner = item
    ram, spr = CAP[name]
    position, radius = draw_position(ram, owner)
    o = EE(ELF, ram, spr)
    o.run(0x1CA7B0, (position,), (radius,))
    f = s32(o.g(2))
    return 'culled' if f < 0 else ('clip' if f & 1 else 'plain')


def original_draw(ram, spr, owner):
    o = EE(ELF, ram, spr)
    ctx = u32(ram, 0x275670)
    o.put32(ctx + 0x10, CAP_DL)
    o.put(CAP_DL, bytes((i * 29 + 7) & 0xFF for i in range(CAP_DL_SIZE)))
    for a, v in ((0x275B48, owner), (0x275B44, owner), (0x275B40, owner + 0x110)):
        o.put32(a, v)
    o.run(DRAW, (owner,))
    return o, ctx


def original_lighting(ram, spr, owner):
    """001D89D0(owner, 0x70003400, 0x70003440, owner + 0x80) after 001D8C20(0),
    as 001CA990 reaches it: the A and B the native chain is answered with."""
    o = EE(ELF, ram, spr)
    ctx = u32(ram, 0x275670)
    for a, v in ((0x275B48, owner), (0x275B44, owner), (0x275B40, owner + 0x110), (ctx + 0x246C, 0)):
        o.put32(a, v)
    o.run(0x1D89D0, (owner, 0x70003400, 0x70003440, owner + 0x80))
    return o.read(0x70003400, 64), o.read(0x70003440, 64)


def case_capture(item):
    name, owner = item
    ram, spr = CAP[name]
    behaviour, count = u32(ram, owner + 0x10), ram[owner + 0x0C]
    o, ctx = original_draw(ram, spr, owner)
    used = o.load(ctx + 0x10) - CAP_DL
    unit = o.read(CAP_DL, used)
    hits = captured_unit(ram, unit) if used else []
    if used and behaviour in REQUIRED or behaviour in ALSO:
        # These behaviours call their draw method every frame they tick; a
        # drawn unit must be in the list. Other owners draw only in some
        # states (e.g. a collected pickup), so their miss is reported.
        assert hits or not used, ('original unit not in the captured list', name, hex(owner), hex(behaviour))
    light, color = original_lighting(ram, spr, owner) if used else (bytes(64), bytes(64))
    if used:
        assert light == o.read(0x70003400, 64) and color == unit[0x20:0x60], ('001D89D0 replay', name, hex(owner))

    # ---- native chain
    bones = (Bone * MAX_BONES)()
    bone_ptr = (C.POINTER(Bone) * MAX_BONES)()
    ow = Owner()
    for field, off, size in OWNER_BYTES:
        setattr(ow, field, int.from_bytes(ram[owner + off:owner + off + size], 'little'))
    for field, off, n in OWNER_FLOATS:
        C.memmove(C.addressof(getattr(ow, field)), ram[owner + off:owner + off + 4 * n], 4 * n)
    handle = C.c_uint32(0)
    assert LIB.em_world_models_001C6120(C.byref(BANK), u32(ram, 0x28A59C), ram[owner + 0x0D], C.byref(handle)) == 0
    assert handle.value == u32(ram, owner + 0x44), ('bank handle', hex(handle.value))
    entry = next(i for i in range(BANK.model_count) if BANK.models[i].address == handle.value)
    ow.model = C.pointer(BANK.models[entry].model)
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
    buf = (C.c_uint8 * CAP_DL_SIZE).from_buffer_copy(bytes((i * 29 + 7) & 0xFF for i in range(CAP_DL_SIZE)))
    ch = (Channel * 1)()
    ch[0].cursor, ch[0].end = C.addressof(buf), C.addressof(buf) + CAP_DL_SIZE
    scratch = Scratch()
    for field, off in (('s3400', 0x3400), ('s3440', 0x3440), ('s3480', 0x3480), ('s3AC0', 0x3AC0)):
        C.memmove(C.addressof(getattr(scratch, field)), spr[off:off + 64], 64)
    view = words(*struct.unpack_from('<16I', ram, 0x810610))
    planes = words(*struct.unpack_from('<16I', ram, ctx + 0x2410))
    c0c, c9c = words(u32(ram, ctx + 0x0C)), words(u32(ram, ctx + 0x9C))
    arena, c50 = words(u32(ram, 0x275674)), words(u32(ram, ctx + 0x50))
    d = Draw()
    d.world.d00810610, d.world.ctx_2410 = C.cast(view, P32), C.cast(planes, P32)
    d.world.ctx_0C, d.world.ctx_9C, d.world.d00275674 = C.cast(c0c, P32), C.cast(c9c, P32), C.cast(arena, P32)
    d.world.channel, d.world.channel_count = C.cast(ch, C.POINTER(Channel)), 1
    d.world.ctx_50, d.world.ctx_50_count = C.cast(c50, P32), 1
    d.world.models = C.pointer(BANK)
    veil = Veil()
    vcur = words(0)
    veil.world.cursor, veil.world.cursor_count = C.cast(vcur, P32), 1
    veil.world.d00275674 = C.cast(arena, P32)
    veil.world.packet, veil.world.packet_address, veil.world.packet_size = C.addressof(buf), CAP_DL, CAP_DL_SIZE
    state = {'mode': None, 'calls': []}

    def w_a7b0(_, pos, radius, out):
        state['calls'].append('a7b0')
        p = C.cast(pos, P32)
        return LIB.em_owner_draw_001CA7B0(C.byref(d), words(p[0], p[1], p[2], 0), radius, out)

    def w_8c20(_, mode):
        state['calls'].append('8c20'); state['mode'] = mode; return 0

    def w_89d0(_, owner_view, a, b):
        state['calls'].append('89d0')
        C.memmove(a, light, 64); C.memmove(b, color, 64); return 0

    def w_1f80(_, a0, a1, a2):
        state['calls'].append('1f80')
        vcur[0] = CAP_DL + (ch[0].cursor - C.addressof(buf))
        rc = LIB.em_load_veil_particles_001D1F80(C.byref(veil), a0, a1, a2)
        ch[0].cursor = C.addressof(buf) + (vcur[0] - CAP_DL)
        return rc

    def w_a940(_, flags, model):
        state['calls'].append('a940')
        return LIB.em_owner_draw_001CA940(C.byref(d), flags, model)

    def refuse(tag):
        def f(*_):
            state['calls'].append(tag); return -1
        return f

    svc = Services()
    svc.world.d00275B40, svc.world.d00275B40_count = C.cast(bone_ptr, C.POINTER(C.POINTER(Bone))), count
    svc.world.scratch = C.pointer(scratch)
    svc.world.channel, svc.world.channel_count = C.cast(ch, C.POINTER(Channel)), 1
    keep = []
    for field, fn in (('w_001CA7B0', w_a7b0), ('w_001D8C20', w_8c20), ('w_001D89D0', w_89d0),
                      ('w_001D1F80', w_1f80), ('w_001CA940', w_a940), ('w_001CB3C0', refuse('b3c0'))):
        cf = WORKER_TYPES[field](fn)
        keep.append(cf)
        setattr(svc.workers, field, cf)
    rc = LIB.em_owner_services_001CAA00(C.byref(svc), C.byref(ow))
    assert rc == 0 and svc.fault.code == 0 and d.fault.code == 0 and veil.fault.code == 0, \
        ('native fault', name, hex(owner), hex(svc.fault.address), svc.fault.code, hex(d.fault.address),
         d.fault.code, veil.fault.code, state['calls'])
    native_used = ch[0].cursor - C.addressof(buf)
    assert native_used == used, ('cursor', name, hex(owner), native_used, used)
    assert bytes(buf) == o.read(CAP_DL, CAP_DL_SIZE), ('native unit != original', name, hex(owner))
    if used:
        assert c50[0] == o.load(ctx + 0x50), ('context +0x50', hex(c50[0]))
        assert state['mode'] == 0 and o.load(ctx + 0x246C) == 0, ('001D8C20 mode', state['mode'])
    kind = 'culled'
    if used:
        calls = [u32(unit, k + 4) for k in range(0, used, 16) if unit[k + 3] == 0x50 and k in tag_offsets(unit)]
        kind = 'clip' if 0x2354A0 in calls else 'plain'
        assert calls[-1:] == [0x2354A0] if kind == 'clip' else calls == [0x23C750], ('kernels', calls)
    return dict(capture=name, owner=hex(owner), behaviour=hex(behaviour), bones=count,
                unit=used, kind=kind, captured_at=[hex(h) for h in hits])


# ---------------------------------------------------------------- main

LIB = ELF = None


def main():
    global LIB, ELF
    started = time.time()
    ELF = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(ELF).hexdigest() == ELF_SHA, 'not the pinned SCUS-97112 ELF'
    LIB = build_library()
    counts = {}

    base = DECOMP / 'build/s87/route'
    for beat in BEATS:
        p = base / beat
        if (p / 'eeMemory.bin').exists() and (p / 'scratchpad.bin').exists():
            ram = (p / 'eeMemory.bin').read_bytes()
            assert ram[0x810700] == 11, (beat, 'not AREA11')
            for lo, hi in ((0x1CA7B0, 0x1CAAC0), (0x1C7420, 0x1C7900), (0x1D1F80, 0x1D1FF0),
                           (0x1D2090, 0x1D2110), (0x1D2710, 0x1D2960), (0x1D37D0, 0x1D3C40),
                           (0x102690, 0x102960), (0x1D89D0, 0x1D8C30)):
                assert ram[lo:hi] == ELF[lo - 0x100000 + 0x300:hi - 0x100000 + 0x300], ('code differs', beat, hex(lo))
            CAP[beat] = (ram, (p / 'scratchpad.bin').read_bytes())
            OWNER_POSITIONS.extend(struct.unpack_from('<3I', ram, a + 0xB0) for _n, a in capture_owners_raw(ram))
    assert CAP, 'no s87 route captures (build/s87/route/*/eeMemory.bin + scratchpad.bin)'
    # A. 001CA7B0
    seeds = list(range(pick(20000, 600)))
    results = parallel_map(case_a7b0, seeds)
    fixed = boundary_inputs()
    results += [case_a7b0_fixed(i) for i in fixed]
    counts['a7b0'] = len(results)
    counts['a7b0_outcomes'] = {str(k): results.count(k) for k in sorted(set(results))}
    for want in (1, 2, 4, 8, 16):
        assert any(r >= 0 and r & want for r in results), ('flag bit never set', want)
    assert -1 in results and 0 in results and 31 in results, 'outcome classes uncovered'

    # B. 001CA940
    items = a940_items()
    chosen = select(items, 160, 0xA940, axes=(lambda i: i[0], lambda i: i[1], lambda i: i[2], lambda i: i[4:]))
    used = parallel_map(case_a940, chosen)
    counts['a940'] = len(used)
    counts['a940_items'] = len(items)

    # C/D. captures and the bank
    load_bank()
    counts['bank_lookups'] = check_bank(CAP[next(iter(CAP))][0])
    asset = ROOT / 'assets/scene_snow/world_models.emwm'
    asset_state = 'absent'
    if asset.exists():
        asset_state = 'current' if asset.read_bytes() == bytes(BANK_BYTES) else 'STALE (re-run export_world_models.py)'
    owners = [o for beat in CAP for o in capture_owners(beat)]

    def behaviour(i): return u32(CAP[i[0]][0], i[1] + 0x10)
    classes = dict(zip(owners, parallel_map(flag_class, owners)))
    chosen = select(owners, 60, 0xCAA00, axes=(behaviour, lambda i: i[0], lambda i: (behaviour(i), classes[i])))
    draws = parallel_map(case_capture, chosen)
    for item, r in zip(chosen, draws):
        assert r['kind'] == classes[item], ('001CA7B0 class and unit kernels disagree', r)
    tally = {}
    for r in draws:
        t = tally.setdefault(r['behaviour'], {'owners': 0, 'plain': 0, 'clip': 0, 'culled': 0, 'in_list': 0})
        t['owners'] += 1
        t[r['kind']] += 1
        t['in_list'] += bool(r['captured_at'])
    for b, label in {**REQUIRED, **ALSO}.items():
        t = tally.get(hex(b))
        assert t and t['plain'] + t['clip'], ('never drawn in the selected captures', label)
    counts['captured_owner_draws'] = len(draws)
    counts['captured_units_drawn'] = sum(1 for r in draws if r['unit'])
    counts['captured_units_in_list'] = sum(1 for r in draws if r['captured_at'])
    line = banner(part(len(seeds), 20000, '001CA7B0 random cases') + f' + {len(fixed)} boundary',
                  part(len(used), len(items), '001CA940 cases'),
                  f"{counts['bank_lookups']} 001C6120 lookups",
                  part(len(draws), len(owners), 'captured owner draws'))
    report = dict(status='PASS', mode=line, elf_sha256=ELF_SHA, counts=counts, bank_asset=asset_state,
                  bank_span_sha256=hashlib.sha256(BANK_X['span']).hexdigest(),
                  captured_by_behaviour=tally, seconds=round(time.time() - started, 1))
    (OUT / 'report.json').write_text(json.dumps(dict(report, draws=draws), indent=1) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    sys.exit(main())
