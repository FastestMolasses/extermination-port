#!/usr/bin/env python3
"""Compare em_crate_original with the owner's original 001551B0 instructions.

The oracle executes 001551B0 and every pure helper it calls from the user's
pinned ELF (001B0FD0, copy_qw4, 00102948, 001029C0, 001029E8, 00102A60,
00102B08, 00102BB0, 00102C58, 001026A0, 001026D0, float_to_int, 001278C0)
over (a) the captured AREA11 RAM images (playable_ee.bin, handoff_ee.bin:
the real D_00275BC0 live list, the four placed records and their fields)
and (b) synthetic states. Model, collision, sound, effect, allocation,
taken-bit and draw functions are explicit worker stubs. After every tick
the test compares every modelled actor field bit-for-bit, the full ordered
worker log with arguments, the alarm bytes of every list node, spawned
child fields, and asserts that the original wrote no byte outside the
modelled fields, the listed nodes, spawned children, the bone matrix,
the stack and the scratchpad. No instruction bytes are copied here.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

from test_item_sdk_math_reference import Original as SdkOriginal
from test_interaction_scan_reference import ELF_SHA, DECOMP
from test_point_light_reference import RETURN, STACK, bits, number, signed, fp
import ee_float_model as M

ROOT = Path(__file__).resolve().parents[1]
CAPTURES = DECOMP/'build/startup-reference'
RAM_SIZE = 0x2000000
CRATE, CRATE_END = 0x1551B0, 0x156614
DRAW, MODEL, BONES, BONE = 0x990000, 0x9A0000, 0x9B0000, 0x9B0100
CHILDREN = 0x9C0000
SYN_ACTOR, SYN_LIST = 0x940000, 0x950000
SYN_REGISTRY = 0x960000
SCRATCH = (0x70000000, 0x70004000)


# ------------------------------------------------------------------ oracle

# The owners' own COP1 arithmetic (add.s / sub.s / mul.s / div.s /
# cvt.s.w inside 001551B0 and 00156620) follows the measured EE model
# (tools/ee_float_model.py, docs/EE_FLOAT_MODEL.md: the pre-trimmed
# truncating sum and product, the round-to-nearest quotient), as the native
# owners compute it through em_ee_float.h. The SDK routines they call keep
# the ITEM SDK oracle's semantics, which their translations were verified
# against.
EE_RANGES = ((CRATE, CRATE_END), (0x156620, 0x156F30))


class Oracle(SdkOriginal):
    """ITEM SDK oracle (64-bit EE scalar semantics, VU0 macro, soft float)
    + indirect calls, call stubs that return to the link address, watch
    points, write logging, pc coverage and an optional captured RAM image.
    COP1 inside EE_RANGES goes through the EE model."""

    def __init__(self, elf, ram=None):
        self.ram = ram
        self.watch = {}
        self.writes = set()
        self.pcs = set()
        self.ee_cop1 = False
        super().__init__(elf)
        self.writes.clear()

    def plain(self, word):
        op, fmt, fn = word >> 26, word >> 21 & 31, word & 63
        if self.ee_cop1 and op == 17 and ((fmt == 16 and fn in (0, 1, 2, 3)) or (fmt == 20 and fn == 32)):
            fs, ft, fd = word >> 11 & 31, word >> 16 & 31, word >> 6 & 31
            a, b = self.f[fs] & 0xFFFFFFFF, self.f[ft] & 0xFFFFFFFF
            if fmt == 20: self.f[fd] = M.ee_cvt_s_w(a)
            elif fn == 0: self.f[fd] = M.ee_add(a, b)
            elif fn == 1: self.f[fd] = M.ee_sub(a, b)
            elif fn == 2: self.f[fd] = M.ee_mul(a, b)
            else: self.f[fd] = M.ee_div(a, b)
            self.r[0] = 0
            return
        super().plain(word)

    def owner_pc(self, pc):
        self.ee_cop1 = any(lo <= pc < hi for lo, hi in EE_RANGES)

    def save(self, address, value, size=4):
        for i in range(size):
            self.writes.add(address + i)
        super().save(address, value, size)

    def load(self, address, size=4):
        value = 0
        for i in range(size):
            a = address + i
            b = self.mem.get(a)
            if b is None:
                if self.ram is not None and a < RAM_SIZE: b = self.ram[a]
                elif 0x100000 <= a < 0x275b00: b = self.elf[a - 0x100000 + 0x300]
                else: b = 0
            value |= b << (8 * i)
        return value

    def f32(self, address): return self.load(address)
    def words(self, address, count): return tuple(self.load(address + 4*i) for i in range(count))
    def ret(self, value): self.r[2] = signed(value & 0xffffffff) & 0xffffffffffffffff
    def arg(self, i): return self.r[4 + i] & 0xffffffff

    def run(self, entry, args=(), floats=(), stop=RETURN):
        self.r[31] = RETURN
        for i, value in enumerate(args): self.r[4 + i] = value
        for i, value in enumerate(floats): self.f[12 + i] = bits(value)
        pc = entry
        for _ in range(3000000):
            if pc == stop: return
            if pc in self.watch: self.watch[pc](self)
            self.pcs.add(pc)
            self.owner_pc(pc)
            word = self.load(pc)
            op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
            offset = signed(word & 65535, 16) * 4
            indirect = op == 0 and word & 63 == 9
            if op in (2, 3) or indirect:
                target = self.r[rs] & 0xffffffff if indirect else (word & 0x3ffffff) * 4
                link = pc + 8
                if indirect: self.r[rd] = link
                elif op == 3: self.r[31] = link
                self.pcs.add(pc + 4)
                self.plain(self.load(pc + 4))
                if target in self.calls:
                    if target in self.watch: self.watch[target](self)
                    self.calls[target](self)
                    pc = link if (op == 3 or indirect) else self.r[31] & 0xffffffff
                else:
                    pc = target
                continue
            branch = None
            if op in (4, 5, 20, 21):
                taken = (self.r[rs] == self.r[rt]) == (op in (4, 20))
                if op in (20, 21) and not taken: pc += 8; continue
                branch = pc + 4 + offset if taken else pc + 8
            elif op in (6, 7):
                taken = signed(self.r[rs], 64) <= 0 if op == 6 else signed(self.r[rs], 64) > 0
                branch = pc + 4 + offset if taken else pc + 8
            elif op == 1:
                assert rt in (0, 1), hex(pc)
                taken = signed(self.r[rs], 64) < 0 if rt == 0 else signed(self.r[rs], 64) >= 0
                branch = pc + 4 + offset if taken else pc + 8
            elif op == 17 and rs == 8:
                taken = self.condition == bool(rt & 1)
                if rt & 2 and not taken: pc += 8; continue
                branch = pc + 4 + offset if taken else pc + 8
            elif op == 0 and word & 63 == 8:
                branch = self.r[rs] & 0xffffffff
            if branch is not None:
                self.pcs.add(pc + 4)
                self.plain(self.load(pc + 4))
                pc = branch
            else:
                try: self.plain(word)
                except AssertionError as error: raise AssertionError(hex(pc), error) from error
                pc += 4
        raise AssertionError(('owner did not return', hex(pc)))


def word_at(elf, pc): return int.from_bytes(elf[pc-0x100000+0x300:pc-0x100000+0x304], 'little')


def branch_targets(elf, start, end):
    targets = set()
    for pc in range(start, end, 4):
        w = word_at(elf, pc); op, rs = w >> 26, w >> 21 & 31
        if op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or (op == 17 and rs == 8):
            targets.add(pc + 4 + signed(w & 0xffff, 16) * 4)
        elif op in (2, 3):
            targets.add((w & 0x3ffffff) * 4)
    return targets


def dead_after_branch(elf, pc, targets):
    """pc follows an unconditional `b` + delay slot and nothing branches to it."""
    return word_at(elf, pc - 8) >> 16 == 0x1000 and pc not in targets


def check_code(elf, ram, pcs):
    """Captured code bytes must be the pinned ELF's for every executed pc."""
    for pc in pcs:
        if 0x100000 <= pc < 0x275b00:
            assert ram[pc:pc+4] == elf[pc-0x100000+0x300:pc-0x100000+0x304], hex(pc)


# ------------------------------------------------------------------ script

class Script:
    """Deterministic worker results, consumed in call order per worker."""

    def __init__(self, seed, **fixed):
        rng = random.Random(seed)
        self.values = {}
        self.fixed = fixed
        self.rng = rng
        self.count = {}

    def take(self, name, generate):
        if name in self.fixed:
            seq = self.fixed[name]
            k = self.count.get(name, 0); self.count[name] = k + 1
            return seq[k % len(seq)] if isinstance(seq, list) else seq
        seq = self.values.setdefault(name, [])
        k = self.count.get(name, 0); self.count[name] = k + 1
        while len(seq) <= k: seq.append(generate(self.rng))
        return seq[k]


def gen_matrix(rng):
    return [rng.choice([0.0, 1.0, -1.0, rng.uniform(-1, 1)]) for _ in range(12)] + \
        [rng.uniform(-400, 400) for _ in range(3)] + [1.0]


def fbits(values): return tuple(bits(v) if isinstance(v, float) else v for v in values)
def fl(words): return [number(w) for w in words]


# ------------------------------------------------------------------ native types

class Crate(C.Structure):
    _fields_ = [(n, C.c_uint8) for n in ('status', 'model', 'state', 'fall_phase', 'break_phase', 'alarm', 'puid')] + [
        ('placement', C.c_uint16), ('tilt', C.c_int16), ('timer', C.c_int16), ('health', C.c_int16),
        ('damage', C.c_int16), ('raised', C.c_uint16), ('link', C.c_int16),
        ('position', C.c_float*4), ('rotation', C.c_float*4), ('world', C.c_float*16),
        ('rest', C.c_float*16), ('step', C.c_float*16), ('spin', C.c_float*3),
        ('velocity', C.c_float*3), ('probe', C.c_float*8)]


class Entry(C.Structure):
    _fields_ = [('model', C.c_uint8), ('raised', C.c_uint16), ('alarm', C.POINTER(C.c_uint8))]


class Registry(C.Structure):
    _fields_ = [('first_group', C.POINTER(C.c_int16)),
                ('groups', C.POINTER(C.POINTER(C.POINTER(C.c_uint8)))), ('area_count', C.c_size_t)]


RATTLE = (C.c_float*3)*4


class Input(C.Structure):
    _fields_ = [('area', C.c_uint8), ('sub_area', C.c_uint8), ('dispatch_has_next', C.c_int),
                ('actors', C.POINTER(Entry)), ('actor_count', C.c_size_t),
                ('registry', C.POINTER(Registry)), ('rattle', C.POINTER(RATTLE)),
                ('rattle_rows', C.c_size_t)]


class Probe(C.Structure):
    _fields_ = [('result', C.c_int32), ('actor', C.c_int32)]


class Child(C.Structure):
    _fields_ = [('class_id', C.c_uint8), ('puid', C.c_uint8), ('model', C.c_uint8),
                ('model_high', C.c_uint16), ('param', C.c_uint8), ('class_two', C.c_uint8),
                ('sub_area', C.c_uint8), ('condition', C.c_uint8), ('placement', C.c_uint16),
                ('kind', C.c_int16), ('link', C.c_int16), ('position', C.c_float*3),
                ('rotation', C.c_float*3), ('behavior', C.c_uint32)]


CALL = C.CFUNCTYPE(C.c_int, C.c_void_p)
MATRIX = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float))
PROBE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_float),
                    C.c_float, C.c_uint32, C.POINTER(Probe))
RANDOM = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_uint32))
SOUND = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint16)
SOUND3D = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint16, C.c_int32, C.c_float)
EFFECT = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_float), C.POINTER(C.c_float))
BYTE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint8)
SPAWN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Child))

HOOK_NAMES = ['allocate_model', 'bone_init', 'publish', 'place', 'probe', 'random', 'sound',
              'sound3d', 'effect', 'taken', 'spawn', 'rebind', 'bone_matrix', 'draw',
              'set_taken', 'free']
HOOK_TYPES = [CALL, CALL, CALL, MATRIX, PROBE, RANDOM, SOUND, SOUND3D, EFFECT, BYTE, SPAWN,
              SOUND, MATRIX, CALL, BYTE, CALL]


class Hooks(C.Structure):
    _fields_ = [('context', C.c_void_p)] + list(zip(HOOK_NAMES, HOOK_TYPES))


FIELDS = [('status', 0, 'B'), ('model', 3, 'B'), ('state', 4, 'B'), ('fall_phase', 5, 'B'),
          ('break_phase', 7, 'B'), ('alarm', 0xA, 'B'), ('puid', 0x9A, 'B'),
          ('placement', 0xE, 'H'), ('tilt', 0x28, 'h'), ('timer', 0x2A, 'h'),
          ('health', 0x34, 'h'), ('damage', 0x36, 'h'), ('raised', 0x52, 'H'),
          ('link', 0x56, 'h'), ('position', 0xB0, 4), ('rotation', 0xC0, 4),
          ('world', 0xD0, 16), ('rest', 0x1F0, 16), ('step', 0x230, 16),
          ('spin', 0x2B8, 3), ('velocity', 0x2C4, 3), ('probe', 0x2D0, 8)]


def field_ranges(fields):
    out = set()
    for _, offset, kind in fields:
        size = {'B': 1, 'H': 2, 'h': 2}.get(kind, 4 * kind if isinstance(kind, int) else 0)
        out.update(range(offset, offset + size))
    return out


def load_fields(o, base, obj, fields):
    for name, offset, kind in fields:
        if isinstance(kind, int):
            arr = getattr(obj, name)
            for i in range(kind):
                arr[i] = number(o.load(base + offset + 4*i))
        else:
            size = 1 if kind == 'B' else 2
            value = o.load(base + offset, size)
            setattr(obj, name, signed(value, 16) if kind == 'h' else value)


def store_fields(o, base, obj, fields):
    for name, offset, kind in fields:
        if isinstance(kind, int):
            for i, v in enumerate(getattr(obj, name)): o.save(base + offset + 4*i, bits(v))
        else:
            size = 1 if kind == 'B' else 2
            o.save(base + offset, getattr(obj, name) & (0xff if size == 1 else 0xffff), size)


def image(obj, fields):
    out = {}
    for name, offset, kind in fields:
        if isinstance(kind, int):
            out[name] = tuple(bits(v) for v in getattr(obj, name))
        else:
            out[name] = getattr(obj, name) & (0xff if kind == 'B' else 0xffff)
    return out


def oracle_image(o, base, fields):
    out = {}
    for name, offset, kind in fields:
        if isinstance(kind, int): out[name] = o.words(base + offset, kind)
        else: out[name] = o.load(base + offset, 1 if kind == 'B' else 2)
    return out


def build():
    out = ROOT/'build/crate_original_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = out/('owners.dylib' if sys.platform == 'darwin' else 'owners.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-fPIC', '-dynamiclib' if sys.platform == 'darwin' else '-shared', '-Isrc',
                    'src/game/em_crate_original.c', 'src/game/em_drum_original.c',
                    'src/game/em_item_sdk_math.c', 'src/game/em_interaction_scan.c',
                    'src/game/em_item_trail.c', '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    native.em_crate_original_tick.argtypes = [C.POINTER(Crate), C.POINTER(Input), C.POINTER(Hooks)]
    return native, out


# ------------------------------------------------------------------ one tick

def probe_result(rng):
    return rng.choice([(0, 0, 0.0), (2, 0, 0.0), (2, 0x7a7c70, 0.0), (4, 0, 0.0), (4, 0x7a7f60, 0.0),
                       (2, 0x1234, 0.0), (6, 0x55, 0.25), (1, 0, 0.0)])


def crate_workers(o, script, log, base, children):
    def expect_self(o): assert o.arg(0) == base, hex(o.arg(0))

    def allocate(o): expect_self(o); log.append(('allocate_model',)); o.ret(script.take('allocate', lambda r: 0))
    def bone(o): expect_self(o); log.append(('bone_init',))
    def publish(o): expect_self(o); log.append(('publish',))
    def place(o):
        expect_self(o); m = script.take('place', gen_matrix)
        for i, v in enumerate(m): o.save(base + 0xD0 + 4*i, bits(v))
        log.append(('place',))
    def probe(o):
        expect_self(o)
        result, actor, snap = script.take('probe', probe_result)
        frm = o.words(o.arg(1), 3); dy = o.load(o.arg(2) + 4); mode = o.arg(3)
        log.append(('probe', frm, dy, mode))
        o.save(0x700031D4, actor)
        if mode & 0x80000000 and result:
            o.save(base + 0xB4, bits(fp(number(o.load(base + 0xB4)) + snap)))
        o.ret(result)
    def rnd(o):
        v = script.take('random', lambda r: r.randrange(0x80000000)); log.append(('random', v)); o.ret(v)
    def sound(o): expect_self(o); log.append(('sound', o.arg(1) & 0xffff))
    def sound3d(o):
        expect_self(o); log.append(('sound3d', o.arg(1) & 0xffff, signed(o.arg(2)), o.f[12]))
    def effect(o):
        assert o.arg(1) == base + 0xB0 and o.arg(2) == base + 0xC0
        log.append(('effect', o.arg(0), o.words(o.arg(1), 4), o.words(o.arg(2), 4)))
    def taken(o):
        puid = o.arg(0) & 0xff; log.append(('taken', puid))
        o.ret(script.take('taken', lambda r: r.choice([0, 0, 1])))
    def alloc(o):
        cls = o.arg(0) & 0xff; ok = script.take('spawn', lambda r: r.choice([1, 1, 0]))
        log.append(('spawn', cls))
        if ok:
            address = CHILDREN + 0x300 * len(children); children.append(address); o.ret(address)
        else: o.ret(0)
    def lookup(o):
        assert o.arg(0) == o.load(0x28a56c); o.rebind_id = o.arg(1) & 0xffff; o.ret(MODEL)
    def bind(o):
        expect_self(o); assert o.arg(1) == MODEL; log.append(('rebind', o.rebind_id))
    def draw(o): expect_self(o); log.append(('draw',))
    def set_taken(o): log.append(('set_taken', o.arg(0) & 0xff))
    def free(o): expect_self(o); log.append(('free',))
    def copy(o):
        if o.arg(0) == BONE + 0x90:
            assert o.arg(1) == base + 0xD0
            log.append(('bone_matrix', o.words(o.arg(1), 16)))
    o.calls.update({0x1b0ea0: allocate, 0x1c62c0: bone, 0x1b1b70: publish, 0x1c6380: place,
                    0x19ab20: probe, 0x122bb8: rnd, 0x1fc580: sound, 0x1fbd50: sound3d,
                    0x1efd90: effect, 0x1b11e0: taken, 0x1afa90: alloc, 0x1c6120: lookup,
                    0x1ca6e0: bind, DRAW: draw, 0x1b1190: set_taken, 0x1afc10: free})
    o.watch[0x102958] = copy


def native_hooks(script, log, crate, spawned):
    def allocate(_): log.append(('allocate_model',)); return script.take('allocate', lambda r: 0)
    def bone(_): log.append(('bone_init',)); return 0
    def publish(_): log.append(('publish',)); return 0
    def place(_, world):
        m = script.take('place', gen_matrix)
        for i, v in enumerate(m): world[i] = v
        log.append(('place',)); return 0
    def probe(_, position, frm, dy, mode, out):
        result, actor, snap = script.take('probe', probe_result)
        log.append(('probe', tuple(bits(frm[i]) for i in range(3)), bits(dy), mode))
        out[0].result, out[0].actor = result, actor
        if mode & 0x80000000 and result: position[1] = fp(position[1] + snap)
        return 0
    def rnd(_, value):
        v = script.take('random', lambda r: r.randrange(0x80000000)); log.append(('random', v))
        value[0] = v; return 0
    def sound(_, i): log.append(('sound', i)); return 0
    def sound3d(_, i, mode, radius): log.append(('sound3d', i, mode, bits(radius))); return 0
    def effect(_, i, pos, rot):
        log.append(('effect', i, tuple(bits(pos[k]) for k in range(4)), tuple(bits(rot[k]) for k in range(4))))
        return 0
    def taken(_, puid):
        log.append(('taken', puid)); return script.take('taken', lambda r: r.choice([0, 0, 1]))
    def spawn(_, child):
        c = child[0]; ok = script.take('spawn', lambda r: r.choice([1, 1, 0]))
        log.append(('spawn', c.class_id))
        if ok: spawned.append(child_tuple(c))
        return ok
    def rebind(_, i): log.append(('rebind', i)); return 0
    def bone_matrix(_, m): log.append(('bone_matrix', tuple(bits(m[k]) for k in range(16)))); return 0
    def draw(_): log.append(('draw',)); return 0
    def set_taken(_, puid): log.append(('set_taken', puid)); return 0
    def free(_): log.append(('free',)); return 0
    fns = [allocate, bone, publish, place, probe, rnd, sound, sound3d, effect, taken, spawn,
           rebind, bone_matrix, draw, set_taken, free]
    keep = [t(f) for t, f in zip(HOOK_TYPES, fns)]
    return Hooks(None, *keep), keep


def child_tuple(c):
    return (c.puid, c.model, c.model_high, c.param,
            (c.sub_area, c.condition) if c.class_two else c.placement, c.kind, c.link,
            tuple(bits(c.position[i]) for i in range(3)), tuple(bits(c.rotation[i]) for i in range(3)),
            c.behavior)


def oracle_child(o, address, class_two):
    return (o.load(address + 0x9A, 1), o.load(address + 3, 1), o.load(address + 0x2E, 2),
            o.load(address + 0xD, 1),
            (o.load(address + 0x9D, 1), o.load(address + 0x9E, 1)) if class_two else o.load(address + 0xE, 2),
            signed(o.load(address + 0x54, 2), 16), signed(o.load(address + 0x56, 2), 16),
            o.words(address + 0xB0, 3), o.words(address + 0xC0, 3), o.load(address + 0x10))


class World:
    """Oracle memory setup shared by one sequence (list, registry, tables)."""

    def __init__(self, elf, ram, base, nodes, area, sub_area):
        self.elf, self.ram, self.base, self.nodes = elf, ram, base, nodes
        self.area, self.sub_area = area, sub_area
        self.o = Oracle(elf, ram)
        self.registry_mem = {}

    def seed(self, o):
        o.save(0x275b40, BONES); o.save(BONES, BONE)
        o.save(self.base + 0x4c, DRAW)
        o.save(0x810700, self.area, 1); o.save(0x810701, self.sub_area, 1)
        for address, value in self.registry_mem.items(): o.save(address, value, 1)


def rattle_table(o):
    table = (RATTLE * 7)()
    for row in range(7):
        for col in range(4):
            for k in range(3):
                table[row][col][k] = number(o.load(0x2468B0 + row*0x30 + col*0xC + 4*k))
    return table


def native_registry(o, area, area_count=23):
    """Native view of D_0024A850/D_0024D820 for one area, read from oracle memory."""
    first = (C.c_int16 * area_count)(*(signed(o.load(0x24A850 + 2*a, 2), 16) for a in range(area_count)))
    keep = [first]
    tables = (C.POINTER(C.POINTER(C.c_uint8)) * area_count)()
    table = o.load(0x24D820 + 4*area)
    if table:
        count = max(first[area], 1) + 8
        groups = (C.POINTER(C.c_uint8) * count)()
        for g in range(count):
            p = o.load(table + 4*g)
            if not p or p >= RAM_SIZE: continue
            data = bytearray()
            for k in range(32):
                rec = bytes(o.load(p + 0x2C*k + i, 1) for i in range(0x2C))
                data += rec
                if struct.unpack_from('<h', rec)[0] == -1: break
            else:
                continue
            buf = (C.c_uint8 * len(data)).from_buffer_copy(data)
            keep.append(buf)
            groups[g] = C.cast(buf, C.POINTER(C.c_uint8))
        keep.append(groups)
        tables[area] = groups
    keep.append(tables)
    reg = Registry(first, tables, area_count)
    keep.append(reg)
    return reg, keep


def list_nodes(o):
    nodes, node = [], o.load(0x275BC0)
    while node and len(nodes) < 512:
        nodes.append(node); node = o.load(node + 0x1C)
    return nodes


def tick_compare(native, world, crate, script_seed, fixed, stats, has_next=1, tag=''):
    """Run one original tick and one native tick from identical state."""
    o, base = world.o, world.base
    o.writes.clear()
    store_fields(o, base, crate, FIELDS)
    world.seed(o)
    o.writes.clear()
    nodes = list_nodes(o)
    before_alarm = {n: o.load(n + 0xA, 1) for n in nodes}
    log_o, children = [], []
    s1 = Script(script_seed, **fixed)
    o.calls.clear(); o.watch.clear()
    crate_workers(o, s1, log_o, base, children)
    # 001AFD70 holds node->next in s0 when it calls the owner.
    o.r[16] = 0x7a0000 if has_next else 0
    o.run(CRATE, (base,))
    # native side
    log_n, spawned = [], []
    s2 = Script(script_seed, **fixed)
    alarms, entries = [], []
    for n in nodes:
        if n == base:
            alarms.append(None)
            entries.append(Entry(crate.model, crate.raised,
                                 C.cast(C.addressof(crate) + Crate.alarm.offset, C.POINTER(C.c_uint8))))
        else:
            cell = C.c_uint8(before_alarm[n]); alarms.append(cell)
            entries.append(Entry(o_load_model(world, n), o_load_raised(world, n), C.pointer(cell)))
    arr = (Entry * max(len(entries), 1))(*entries)
    reg, keep = native_registry(o, world.area)
    table = rattle_table(o)
    inp = Input(world.area, world.sub_area, has_next, arr, len(entries), C.pointer(reg),
                C.cast(table, C.POINTER(RATTLE)), 7)
    hooks, keep2 = native_hooks(s2, log_n, crate, spawned)
    result = native.em_crate_original_tick(C.byref(crate), C.byref(inp), C.byref(hooks))
    assert result >= 0, (tag, 'native fault', log_n, log_o)
    # compare
    expected = oracle_image(o, base, FIELDS)
    actual = image(crate, FIELDS)
    diff = {k: (actual[k], expected[k]) for k in expected if actual[k] != expected[k]}
    assert log_n == log_o and not diff, dict(tag=tag, diff=diff, native=log_n, original=log_o)
    assert (result == 0) == (('free',) in log_o), tag
    for n, cell in zip(nodes, alarms):
        if cell is not None:
            assert cell.value == o.load(n + 0xA, 1), (tag, hex(n))
    expected_children = [oracle_child(o, a, isinstance(c[4], tuple))
                         for a, c in zip(children, spawned)]
    assert len(children) == len(spawned) and expected_children == spawned, (tag, expected_children, spawned)
    # every original write is accounted for
    allowed = {base + off for off in field_ranges(FIELDS)}
    allowed |= {n + 0xA for n in nodes}
    allowed |= set(range(BONE + 0x90, BONE + 0xD0))
    for a in children: allowed |= set(range(a, a + 0x300))
    stray = sorted(a for a in o.writes if a not in allowed and not SCRATCH[0] <= a < SCRATCH[1]
                   and not STACK - 0x2000 <= a < STACK)
    assert not stray, (tag, [hex(a) for a in stray[:16]])
    stats['ticks'] += 1
    stats['calls'] += len(log_o)
    stats['children'] += len(children)
    return result, log_o


def o_load_model(world, n): return world.o.load(n + 3, 1)
def o_load_raised(world, n): return world.o.load(n + 0x52, 2)


def crate_from(o, base):
    c = Crate(); load_fields(o, base, c, FIELDS); return c


# ------------------------------------------------------------------ scenarios

def captured(elf, native, stats):
    """The four AREA11 placements in each capture, over the real list."""
    names = {}
    for capture in ('playable_ee.bin', 'handoff_ee.bin'):
        ram = (CAPTURES/capture).read_bytes()
        names[capture] = hashlib.sha256(ram).hexdigest()
        probe = Oracle(elf, ram)
        nodes = list_nodes(probe)
        crates = [n for n in nodes if probe.load(n + 0x10) == CRATE]
        assert len(crates) == 4, (capture, [hex(n) for n in crates])
        assert (ram[0x810700], ram[0x810701]) == (11, 0)
        for base in crates:
            c0 = crate_from(probe, base)
            assert (c0.model, c0.state, c0.placement & 1, c0.link) == (6, 4, 0, -1)
            scenarios = [
                ('rest', [dict()] * 3),
                ('shot', [dict(damage=1)] + [dict()] * 4),
                ('alarm-held', [dict(alarm=1)] + [dict()] * 8),
                ('alarm-fall', [dict(alarm=1)] + [dict()] * 45),
                ('init', [dict(state=0)] + [dict()] * 3),
            ]
            for name, steps in scenarios:
                world = World(elf, ram, base, nodes, 11, 0)
                crate = crate_from(world.o, base)
                if name == 'alarm-held': fixed = dict(probe=[(2, 0x7a7f60, 0.0)])
                elif name == 'alarm-fall':
                    # one supporting corner, then the landing probe reports world ground
                    fixed = dict(probe=[(2, 0x7a7f60, 0.0), (0, 0, 0.0), (0, 0, 0.0), (0, 0, 0.0),
                                        (0, 0, 0.0)] + [(0, 0, 0.0)] * 12 + [(4, 0, 0.0)])
                else: fixed = dict(probe=[(4, 0, 0.0), (2, 0x7a7c70, 0.0)])
                fixed['place'] = [list(crate.world)]
                for k, overrides in enumerate(steps):
                    for field, value in overrides.items(): setattr(crate, field, value)
                    seed = base ^ (len(name) << 20) ^ (k << 8)
                    result, log = tick_compare(native, world, crate, seed,
                                               fixed, stats, tag=(capture, hex(base), name, k))
                    stats['captured_ticks'] += 1
                    if result == 0: break
                    # carry the ORIGINAL state forward (identical to native after the compare)
                check_code(elf, ram, world.o.pcs)
                stats['pcs'] |= world.o.pcs
        # the alarm reaches exactly the raised crate(s) of the real list
        world = World(elf, ram, crates[1], nodes, 11, 0)
        crate = crate_from(world.o, crates[1]); crate.damage = 1
        tick_compare(native, world, crate, 7, dict(), stats, tag=(capture, 'broadcast'))
        woken = [hex(n) for n in nodes if world.o.load(n + 0xA, 1)]
        stats['captured_broadcast_' + capture] = woken
    return names


def synthetic_registry(world, rng, link):
    """Registry for world.area at synthetic addresses behind D_0024A850/D_0024D820."""
    area = world.area
    base_index = rng.choice([0, 1, 3])
    mem = world.registry_mem
    def put(address, value, size):
        for i in range(size): mem[address + i] = (value >> (8*i)) & 0xff
    put(0x24A850 + 2*area, base_index, 2)
    put(0x24D820 + 4*area, SYN_REGISTRY, 4)
    records = SYN_REGISTRY + 0x1000
    put(SYN_REGISTRY + 4*(max(base_index, 1) + link), records, 4)
    count = rng.randrange(0, 5)
    for k in range(count):
        rec = records + 0x2C*k
        put(rec, rng.choice([0, 1, 5]), 2)
        put(rec + 2, rng.randrange(1, 255), 1)
        put(rec + 4, rng.choice([2, 0x22, 4, 0x82, 0x0B]), 1)
        put(rec + 5, rng.choice([0, 0, 1]), 1)
        put(rec + 6, rng.choice([0x0100, 0x2C0D, 0xFF06, 0x8012]), 2)
        put(rec + 8, rng.randrange(256), 1)
        put(rec + 0xA, rng.randrange(0x10000), 2)
        put(rec + 0xC, rng.randrange(-5, 0x60) & 0xffff, 2)
        put(rec + 0xE, rng.randrange(-1, 4) & 0xffff, 2)
        for i in range(6): put(rec + 0x10 + 4*i, bits(rng.uniform(-50, 50)), 4)
        put(rec + 0x28, rng.choice([0x1551B0, 0x153F10, 0x15A2C0]), 4)
    put(records + 0x2C*count, 0xffff, 2)


def synthetic(elf, native, stats, count):
    rng = random.Random(0x1551B0)
    for case in range(count):
        base = SYN_ACTOR
        area = rng.choice([2, 11, 17])
        world = World(elf, None, base, None, area, rng.randrange(8))
        o = world.o
        # live list: self + neighbours
        nodes = [base] + [SYN_LIST + 0x100*i for i in range(rng.randrange(0, 5))]
        rng.shuffle(nodes)
        o.save(0x275BC0, nodes[0])
        for a, b in zip(nodes, nodes[1:] + [0]): o.save(a + 0x1C, b)
        for n in nodes:
            if n == base: continue
            o.save(n + 3, rng.choice([6, 0x1C, 0x1D, 0x1E, 0x1F, 0x50, 0x18]), 1)
            o.save(n + 0x52, rng.choice([0, 1, 0x100]), 2)
            o.save(n + 0xA, rng.choice([0, 0, 1]), 1)
        crate = Crate()
        crate.model = rng.choice([6, 6, 0x1C, 0x1E, 0x1F, 0x50, 0x0D])
        crate.state = rng.choice([0, 1, 1, 2, 2, 3, 4, 4, 5])
        crate.fall_phase = rng.choice([0, 0, 1, 2])
        crate.break_phase = rng.choice([0, 1, 1, 2])
        crate.alarm = rng.choice([0, 1])
        crate.puid = rng.choice([0, 7])
        crate.placement = rng.choice([0, 1, 0x700, 0x701])
        crate.tilt = rng.choice([0, 1, 2, 3, 30, -1])
        crate.timer = rng.choice([0, 1, 2, 6, 180, -1, 1019])
        crate.health = 1
        crate.damage = rng.choice([0, 0, 1, 0x2000, -5])
        crate.raised = rng.choice([0, 1])
        crate.link = rng.choice([-1, -1, 0, 2])
        crate.status = rng.choice([0, 1])
        for i in range(3): crate.position[i] = rng.uniform(-400, 400)
        crate.position[3] = rng.choice([1.0, 0.0])
        for i in range(4): crate.rotation[i] = rng.choice([0.0, rng.uniform(-3, 3)])
        for arr in (crate.world, crate.rest, crate.step):
            m = gen_matrix(rng)
            for i in range(16): arr[i] = m[i]
        for i in range(3): crate.spin[i] = rng.choice([0.0, rng.uniform(-0.1, 0.1)])
        for i in range(3): crate.velocity[i] = rng.choice([0.0, rng.uniform(-2, 2)])
        for i in range(8): crate.probe[i] = rng.uniform(-400, 400)
        if crate.state == 4 and rng.random() < 0.7:
            wait = rng.choice([-1, 0, 1, 2, 3, 5, 7, 8, 9, 200])
            row = rng.randrange(7)
            raw = (C.c_int32 * 2)(wait, row)
            C.memmove(C.addressof(crate.step) + 8, raw, 8)
        if crate.link >= 0:
            synthetic_registry(world, rng, crate.link)
        fixed = {}
        if rng.random() < 0.3: fixed['allocate'] = [rng.choice([0, 1])]
        for k in range(rng.randrange(1, 8)):
            result, log = tick_compare(native, world, crate, rng.randrange(1 << 30), fixed, stats,
                                       has_next=rng.choice([0, 1]), tag=('synthetic', case, k))
            if result == 0: break
            if rng.random() < 0.15: crate.damage = 1
            if rng.random() < 0.1: crate.alarm = 1
        stats['synthetic_sequences'] += 1
        stats['pcs'] |= world.o.pcs


def validate_math(elf, native):
    """Exported SDK helpers against their original instructions."""
    rng = random.Random(0x102B08)
    lib = native
    lib.em_crate_sdk_rotate.argtypes = [C.POINTER(C.c_float), C.POINTER(C.c_float), C.c_float, C.c_int]
    lib.em_crate_sdk_euler.argtypes = [C.POINTER(C.c_float), C.POINTER(C.c_float)]
    lib.em_crate_sdk_multiply.argtypes = [C.POINTER(C.c_float)] * 3
    lib.em_crate_sdk_wrap.argtypes = [C.c_float]; lib.em_crate_sdk_wrap.restype = C.c_float
    lib.em_crate_sdk_float_to_int.argtypes = [C.c_float]; lib.em_crate_sdk_float_to_int.restype = C.c_int32
    cases = 0
    for _ in range(600):
        m = (C.c_float * 16)(*gen_matrix(rng))
        angle = rng.choice([0.0, -0.0, 1.5707964, -1.5707964, 3.1415927, 4.712389, 0.05235988,
                            -0.05235988, rng.uniform(-7, 7)])
        for axis, entry in ((0, 0x102B08), (1, 0x102BB0), (2, 0x102A60)):
            o = Oracle(elf)
            for i in range(16): o.save(0x500000 + 4*i, bits(m[i]))
            o.run(entry, (0x500040, 0x500000), (angle,))
            out = (C.c_float * 16)()
            lib.em_crate_sdk_rotate(out, m, angle, axis)
            assert tuple(bits(v) for v in out) == o.words(0x500040, 16), (axis, angle)
            cases += 1
        e = (C.c_float * 3)(*(rng.uniform(-0.2, 0.2) for _ in range(3)))
        o = Oracle(elf)
        for i in range(16): o.save(0x500000 + 4*i, bits(m[i]))
        for i in range(3): o.save(0x500100 + 4*i, bits(e[i]))
        o.run(0x102C58, (0x500000, 0x500000, 0x500100))
        mm = (C.c_float * 16)(*m)
        lib.em_crate_sdk_euler(mm, e)
        assert tuple(bits(v) for v in mm) == o.words(0x500000, 16)
        value = rng.choice([rng.uniform(-40, 40), 3.1415927, -3.1415927, 3.1415928, -3.1415928,
                            rng.uniform(-1e4, 1e4)])
        o = Oracle(elf); o.run(0x1B1470, floats=(value,))
        assert bits(lib.em_crate_sdk_wrap(value)) == o.f[0], value
        value = rng.choice([rng.uniform(-3e9, 3e9), rng.uniform(-70000, 70000), 0.5, -0.5,
                            2147483648.0, -2147483648.0, 1019.9])
        o = Oracle(elf); o.run(0x1281C0, floats=(value,))
        assert lib.em_crate_sdk_float_to_int(value) == signed(o.r[2]), value
        cases += 3
    return cases


def main():
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    native, out = build()
    stats = dict(ticks=0, calls=0, children=0, captured_ticks=0, synthetic_sequences=0, pcs=set())
    math_cases = validate_math(elf, native)
    captures = captured(elf, native, stats)
    synthetic(elf, native, stats, int(sys.argv[1]) if len(sys.argv) > 1 else 1500)
    owner = {pc for pc in stats['pcs'] if CRATE <= pc < CRATE_END}
    total = (CRATE_END - CRATE) // 4
    missing = sorted(pc for pc in range(CRATE, CRATE_END, 4) if pc not in owner)
    targets = branch_targets(elf, CRATE, CRATE_END)
    reachable_missing = [hex(pc) for pc in missing if not dead_after_branch(elf, pc, targets)]
    assert not reachable_missing, reachable_missing
    report = dict(status='PASS', sdk_math_cases=math_cases, ticks=stats['ticks'],
                  captured_ticks=stats['captured_ticks'], synthetic_sequences=stats['synthetic_sequences'],
                  worker_calls=stats['calls'], spawned_children=stats['children'],
                  owner_instructions=total, executed=len(owner),
                  unexecuted=[hex(pc) for pc in missing],
                  broadcast_playable=stats.get('captured_broadcast_playable_ee.bin'),
                  broadcast_handoff=stats.get('captured_broadcast_handoff_ee.bin'),
                  captures=captures, elf_sha256=ELF_SHA)
    (out/'crate_report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f"Original crate owner 001551B0: PASS {stats['ticks']} ticks "
          f"({stats['captured_ticks']} captured, {stats['synthetic_sequences']} synthetic sequences), "
          f"{stats['calls']} worker calls, {math_cases} SDK math cases, "
          f"{len(owner)}/{total} owner instructions executed")


if __name__ == '__main__': main()
