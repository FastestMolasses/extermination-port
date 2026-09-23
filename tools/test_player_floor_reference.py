#!/usr/bin/env python3
"""Execute the original floor/fall routines and compare em_player_floor.c.

WP-15 P17/P18. The user's pinned ELF supplies every instruction; none are
embedded here. The bounded interpreter (test_player_reversal_reference
Reversal) runs, unmodified:

  001796C0  fall-state check        00179450  floor-table query
  00179680  fall-state entry        00175900  floor service
  00175CF0  floor-hit apply

Hooked boundaries, scripted per case and recorded (never simulated):
0019BC40 column-table rebuild (writes the case's table into the original
scratchpad tables), 0019AB20 / 0019B6C0 / 0019B8C0 probes (write the case's
hit record into the original scratchpad result block), 0019A310 slope,
0011E620 atan2, 0011E398 cosine, 00175640 link test, 0017F9E0 / 0017FB90
surface-0x39 handlers and 00187DC0 / 00187DE0 / 00187EA0 one-shots.

Cases: random synthetic actors, and the captured first-control actor bytes
(collision_run_poll.json) as the starting RAM.
"""
import ctypes as C
import hashlib
import json
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_player_reversal_reference import Reversal, ELF_SHA256  # noqa: E402
from test_point_light_reference import bits, number, signed, fp  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
REFERENCE = DECOMP / 'build/startup-reference'
ACTOR = 0x680000
NODE = 0x6A0000          # synthetic hit node (0x700031D0 target)
ENTITY = 0x6B0000        # synthetic hit entity (0x700031D4 target)
LINK = 0x6C0000          # synthetic nav link (+214 target)

FALL_CHECK, FLOOR_QUERY, FALL_ENTER = 0x1796C0, 0x179450, 0x179680
COLUMN, GROUND = 0x19BC40, 0x19AB20
FLOOR_SERVICE, FLOOR_APPLY = 0x175900, 0x175CF0
HEAD, OBJECT, LINK_TEST = 0x19B6C0, 0x19B8C0, 0x175640
SURFACE39 = (0x17F9E0, 0x17FB90)
FIRST_CONTACT = {0x187DC0: 0x5A, 0x187DE0: 0x5B, 0x187EA0: 0x5C}
ATAN2, COSINE, ATAN, SQRT = 0x11E620, 0x11E398, 0x11DBB8, 0x11E748
LIBC = C.CDLL(None)
for _name in ('atan2f',): getattr(LIBC, _name).argtypes = [C.c_float, C.c_float]
for _name in ('cosf', 'atanf', 'sqrtf'): getattr(LIBC, _name).argtypes = [C.c_float]
for _name in ('atan2f', 'cosf', 'atanf', 'sqrtf'): getattr(LIBC, _name).restype = C.c_float


class ProbeHit(C.Structure):
    _fields_ = [('kind', C.c_int), ('node', C.c_uint16), ('entity_flags', C.c_uint8),
                ('entity_type', C.c_uint8), ('entity', C.c_int), ('point', C.c_float * 3),
                ('delta', C.c_float * 3), ('normal', C.c_float * 3), ('axis', C.c_float * 3)]


class FloorTable(C.Structure):
    _fields_ = [('count', C.c_int), ('flags', C.c_uint16 * 20), ('height', C.c_float * 20),
                ('aux', C.c_float * 20)]


class FallActor(C.Structure):
    _fields_ = [('position', C.c_float * 3), ('probe', C.c_float * 3), ('drop', C.c_float),
                ('below', C.c_float), ('lock', C.c_uint8), ('mode', C.c_uint8),
                ('contact', C.c_uint8), ('state', C.c_uint8), ('walk', C.c_uint8),
                ('slide', C.c_uint8), ('row', C.c_uint8), ('special', C.c_uint8)]


COLUMN_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float), C.POINTER(FloorTable))
GROUND_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_float),
                        C.c_uint, C.POINTER(ProbeHit))


class FallWorkers(C.Structure):
    _fields_ = [('context', C.c_void_p), ('column', COLUMN_FN), ('ground', GROUND_FN)]


class FloorActor(C.Structure):
    _fields_ = [('position', C.c_float * 3), ('probe', C.c_float * 3), ('yaw', C.c_float),
                ('pitch', C.c_float), ('slope', C.c_float), ('surface_y', C.c_float),
                ('conveyor_yaw', C.c_float), ('slide_yaw', C.c_float),
                ('surface_class', C.c_uint16), ('mode', C.c_uint8), ('surface_mode', C.c_uint8),
                ('surface', C.c_uint8), ('contact', C.c_uint8), ('floor_hit', C.c_uint8),
                ('depth', C.c_uint8), ('puddle', C.c_uint8), ('marsh', C.c_uint8),
                ('lock', C.c_uint8), ('slide', C.c_uint8), ('major', C.c_uint8), ('state', C.c_uint8),
                ('link', C.c_uint8), ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


PROBE2_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_float),
                        C.POINTER(ProbeHit))
LINK_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_int))
HANDLER_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int)
CONTACT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint8)
MATH2_FN = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float, C.c_float)
MATH1_FN = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float)


class FloorWorkers(C.Structure):
    _fields_ = [('context', C.c_void_p), ('ground', GROUND_FN), ('head', PROBE2_FN),
                ('object', GROUND_FN), ('link_test', LINK_FN), ('surface39', HANDLER_FN),
                ('first_contact', CONTACT_FN), ('atan2', MATH2_FN), ('cosine', MATH1_FN),
                ('atan', MATH1_FN), ('sqrt', MATH1_FN)]


# (field, offset, size, float) for 00175900/00175CF0; +214 is a pointer.
FLOOR_FIELDS = (('yaw', 0xC4, 4, True), ('pitch', 0xC0, 4, True), ('slope', 0x9C, 4, True),
                ('surface_y', 0x250, 4, True), ('conveyor_yaw', 0x310, 4, True),
                ('slide_yaw', 0x218, 4, True), ('surface_class', 0x238, 2, False),
                ('mode', 0x1F0, 1, False), ('surface_mode', 0x23B, 1, False),
                ('surface', 0x23A, 1, False), ('contact', 0xA, 1, False), ('floor_hit', 0xB, 1, False),
                ('depth', 0x23C, 1, False), ('puddle', 0x23D, 1, False), ('marsh', 0x23E, 1, False),
                ('lock', 0x25F, 1, False), ('slide', 0x237, 1, False), ('major', 4, 1, False),
                ('state', 5, 1, False))

FALL_FIELDS = (('drop', 0x2EC, 4, True), ('below', 0x258, 4, True), ('lock', 0x25F, 1, False),
               ('mode', 0x1F0, 1, False), ('contact', 0xA, 1, False), ('state', 5, 1, False),
               ('walk', 6, 1, False), ('slide', 0x237, 1, False), ('row', 0x235, 1, False),
               ('special', 0x236, 1, False))


class Floor(Reversal):
    def __init__(self, elf, ram=None):
        super().__init__(elf, b'')
        self.ram = ram
        self.log = []
        self.tables = []      # scripted 0019BC40 results, in call order
        self.probes = []      # scripted probe results, in call order
        self.probe_count = 0
        self.links = []       # scripted 00175640 results
        self.calls = {
            COLUMN: Floor.column,
            GROUND: lambda o: o.probe('ground'),
            HEAD: lambda o: o.probe('head'),
            OBJECT: lambda o: o.probe('object'),
            LINK_TEST: Floor.link_test,
            SURFACE39[0]: lambda o: o.record_call('surface39', 0),
            SURFACE39[1]: lambda o: o.record_call('surface39', 1),
            ATAN2: lambda o: o.ret_float(LIBC.atan2f(number(o.f[12]), number(o.f[13]))),
            COSINE: lambda o: o.ret_float(LIBC.cosf(number(o.f[12]))),
            ATAN: lambda o: o.ret_float(LIBC.atanf(number(o.f[12]))),
            SQRT: lambda o: o.ret_float(LIBC.sqrtf(number(o.f[12]))),
        }
        for address, surface in FIRST_CONTACT.items():
            self.calls[address] = (lambda value: lambda o: o.record_call('first_contact', value))(surface)

    def record_call(self, *entry):
        self.log.append(entry)
        self.r[2] = 0

    def link_test(self):
        self.log.append(('link_test', self.r[4]))
        self.r[2] = self.links.pop(0)

    def plain(self, word):
        op, fmt, fn = word >> 26, word >> 21 & 31, word & 63
        if op == 17 and fmt == 16 and fn in (0x1A, 0x1C):   # mula.s / madd.s
            fs, ft, fd = word >> 11 & 31, word >> 16 & 31, word >> 6 & 31
            product = fp(number(self.f[fs]) * number(self.f[ft]))
            if fn == 0x1A:
                self.fpu_acc = product
            else:
                self.f[fd] = bits(fp(self.fpu_acc + product))
            return
        super().plain(word)

    def load(self, address, size=4):
        if self.ram is not None and address not in self.mem and address < len(self.ram):
            if not 0x100000 <= address < 0x275b00:
                return int.from_bytes(self.ram[address:address + size], 'little')
        return super().load(address, size)

    def vector(self, address, count=3):
        return tuple(self.load(address + 4 * i) for i in range(count))

    def column(self):
        table = self.tables.pop(0)
        self.log.append(('column', self.vector(self.r[4])))
        self.save(0x700031E0, table.count & 0xffffffff)
        for i in range(20):
            self.save(0x70003170 + 2 * i, table.flags[i], 2)
            self.save(0x700030F0 + 4 * i, bits(table.height[i]))
            self.save(0x282250 + 4 * i, bits(table.aux[i]))

    def probe(self, name):
        hit = self.probes.pop(0)
        if name == 'ground':
            self.log.append(('ground', self.vector(self.r[5]), self.vector(self.r[6]), self.r[7]))
        elif name == 'head':
            self.log.append(('head', self.vector(self.r[4]), self.vector(self.r[5])))
        elif name == 'object':
            self.log.append(('object', self.vector(self.r[5]), self.vector(self.r[6]), self.r[7]))
        self.save(0x700031D8, hit.kind)
        if hit.kind:
            self.save(0x700031D0, NODE)
            self.save(NODE + 0x1A, hit.node, 2)
            for i in range(3):
                self.save(NODE + 0x24 + 4 * i, bits(hit.normal[i]))
                self.save(NODE + 0x34 + 4 * i, bits(hit.axis[i]))
                self.save(0x700031B0 + 4 * i, bits(hit.point[i]))
                self.save(0x700031C0 + 4 * i, bits(hit.delta[i]))
            entity = ENTITY + 0x100 * self.probe_count
            self.probe_count += 1
            self.save(0x700031D4, entity if hit.entity else 0)
            self.save(entity + 2, hit.entity_flags, 1)
            self.save(entity + 3, hit.entity_type, 1)
        else:
            self.save(0x700031D0, 0)
        self.r[2] = hit.kind


def build_native():
    out = ROOT / 'build/player_floor_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('floor.dylib' if sys.platform == 'darwin' else 'floor.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_player_floor.c', '-lm', '-o', str(lib)],
                   cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    native.em_player_fall_check.argtypes = [C.POINTER(FallActor), C.POINTER(FallWorkers)]
    native.em_player_floor_query.argtypes = [C.POINTER(FallActor), C.POINTER(FloorTable)]
    native.em_player_fall_enter.argtypes = [C.POINTER(FallActor)]
    native.em_player_floor_service.argtypes = [C.POINTER(FloorActor), C.c_int, C.POINTER(C.c_float),
                                               C.POINTER(FloorWorkers)]
    return out, native


def random_hit(rng, kind=None):
    hit = ProbeHit()
    hit.kind = rng.choice([0, 2, 4, 1]) if kind is None else kind
    hit.node = rng.choice([0x1000, 0x2000, 0x4000, 0x8000, 0x0800, 0x1005, 0x4005, 0x4035,
                           0x4039, 0x405A, 0x405B, 0x405C, 0x1035, 0x2003, rng.randrange(65536)])
    hit.entity = rng.randrange(2)
    hit.entity_flags = rng.choice([2, 4, 0x24, 0xE2, rng.randrange(256)])
    hit.entity_type = rng.choice([2, 6, 10, 12, 24, 40, 42, 0x54, rng.randrange(256)])
    for i in range(3):
        hit.point[i] = rng.uniform(-300, 300)
        hit.delta[i] = rng.choice([0.0, rng.uniform(-2, 2), rng.uniform(-14, 14)])
        hit.normal[i] = rng.uniform(-1, 1)
    return hit


def random_table(rng):
    table = FloorTable()
    table.count = rng.choice([0, 1, 2, 3, 5, 20, rng.randrange(21)])
    for i in range(20):
        table.flags[i] = rng.choice([0, 1, 0x8001, 0x4001, 0x8000, 0x4000, 0x81, rng.randrange(65536)])
        table.height[i] = rng.uniform(150, 260)
        table.aux[i] = rng.choice([0.0, 0.5, 0.62831855, number(bits(0.62831855) - 1), 1.0,
                                   rng.uniform(-2, 2)])
    return table


# ---------------------------------------------------------------- 001796C0

def fall_case(elf, native, actor, tables, probes, ram=None):
    oracle = Floor(elf, ram)
    for name, offset, size, is_float in FALL_FIELDS:
        value = getattr(actor, name)
        oracle.save(ACTOR + offset, bits(value) if is_float else value, size)
    for i in range(3):
        oracle.save(ACTOR + 0xB0 + 4 * i, bits(actor.position[i]))
        oracle.save(ACTOR + 0x280 + 4 * i, bits(actor.probe[i]))
    oracle.tables = list(tables); oracle.probes = list(probes)
    oracle.call(FALL_CHECK, ACTOR)
    native_log = []
    table_queue, probe_queue = list(tables), list(probes)

    def column(_, position, out):
        native_log.append(('column', tuple(bits(position[i]) for i in range(3))))
        table = table_queue.pop(0)
        C.memmove(out, C.byref(table), C.sizeof(FloorTable))
        return 0

    def ground(_, position, probe, mask, out):
        native_log.append(('ground', tuple(bits(position[i]) for i in range(3)),
                           tuple(bits(probe[i]) for i in range(3)), mask))
        hit = probe_queue.pop(0)
        C.memmove(out, C.byref(hit), C.sizeof(ProbeHit))
        return hit.kind

    workers = FallWorkers(None, COLUMN_FN(column), GROUND_FN(ground))
    assert native.em_player_fall_check(C.byref(actor), C.byref(workers)) == 0
    expected = [(e[0], e[1]) if e[0] == 'column' else e for e in oracle.log]
    assert expected == native_log, (expected, native_log)
    for name, offset, size, is_float in FALL_FIELDS:
        raw = oracle.load(ACTOR + offset, size)
        value = getattr(actor, name)
        got = bits(value) if is_float else value
        assert got == raw, (name, hex(got), hex(raw))
    for i in range(3):
        assert bits(actor.position[i]) == oracle.load(ACTOR + 0xB0 + 4 * i), ('position', i)
    return oracle, native_log


def random_fall_actor(rng):
    actor = FallActor()
    for i in range(3):
        actor.position[i] = rng.uniform(-300, 300)
    actor.probe[0], actor.probe[1], actor.probe[2] = 0.0, rng.choice([-13.8, -13.8, -5.0, 3.0]), 0.0
    actor.drop = rng.choice([0.0, -0.04, -0.08, number(bits(-0.08) + 1), -0.12, number(bits(-0.12) - 1),
                             -0.16, -3.99, -4.0, rng.uniform(-5, 0.1)])
    actor.below = rng.uniform(-10, 10)
    actor.lock = rng.choice([0, 0, 0, 2, 1])
    actor.mode = rng.choice([0, 1, 2, 0x3A, 0x3A, 0x30, 11, rng.randrange(256)])
    actor.contact = rng.choice([0, 0, 0, 1, 2, 3, 0x81])
    actor.state = rng.choice([0, 1, 5, 0x1C, 0x1D, 0x1E, rng.randrange(256)])
    actor.walk = rng.randrange(256)
    actor.slide = rng.choice([0, 0, 1, rng.randrange(256)])
    actor.row = rng.randrange(256)
    actor.special = rng.choice([0, 0, 1, rng.randrange(256)])
    return actor



# ---------------------------------------------------------------- 00175900

def floor_case(elf, native, actor, search, probes, links, at_seed, ram=None, base=ACTOR):
    oracle = Floor(elf, ram)
    for name, offset, size, is_float in FLOOR_FIELDS:
        value = getattr(actor, name)
        oracle.save(base + offset, bits(value) if is_float else value, size)
    for i in range(3):
        oracle.save(base + 0xB0 + 4 * i, bits(actor.position[i]))
        oracle.save(base + 0x280 + 4 * i, bits(actor.probe[i]))
    oracle.save(base + 0x214, LINK if actor.link else 0)
    oracle.save(base + 0xA, actor.contact, 1)
    oracle.save(LINK + 2, actor.link_flags, 1); oracle.save(LINK + 3, actor.link_type, 1)
    oracle.probes = list(probes); oracle.links = list(links)
    oracle.call(FLOOR_SERVICE, base, search)
    returned = oracle.r[2] & 0xFF

    log = []
    queue, link_queue = list(probes), list(links)

    def take(out):
        hit = queue.pop(0)
        C.memmove(out, C.byref(hit), C.sizeof(ProbeHit))
        return hit.kind

    def vec(v): return tuple(bits(v[i]) for i in range(3))
    def ground(_, position, probe, mask, out):
        log.append(('ground', vec(position), vec(probe), mask)); return take(out)
    def head(_, top, bottom, out):
        log.append(('head', vec(top), vec(bottom))); return take(out)
    def obj(_, at, probe, mask, out):
        log.append(('object', vec(at), vec(probe), mask)); return take(out)
    def link_test(_, result):
        log.append(('link_test',)); result[0] = link_queue.pop(0); return 0
    def surface39(_, which):
        log.append(('surface39', which)); return 0
    def first_contact(_, surface):
        log.append(('first_contact', surface)); return 0
    workers = FloorWorkers(None, GROUND_FN(ground), PROBE2_FN(head), GROUND_FN(obj),
                           LINK_FN(link_test), HANDLER_FN(surface39), CONTACT_FN(first_contact),
                           MATH2_FN(lambda _, y, x: LIBC.atan2f(y, x)),
                           MATH1_FN(lambda _, x: LIBC.cosf(x)), MATH1_FN(lambda _, x: LIBC.atanf(x)),
                           MATH1_FN(lambda _, x: LIBC.sqrtf(x)))
    at = (C.c_float * 3)(*at_seed)
    result = native.em_player_floor_service(C.byref(actor), search, at, C.byref(workers))
    assert result == returned, ('returned', result, returned)
    expected = [('link_test',) if entry[0] == 'link_test' else entry for entry in oracle.log]
    floor_hit = any(e[0] == 'ground' for e in log) and any(
        p.kind for p in probes[:len([e for e in log if e[0] == 'ground'])])
    if not floor_hit:   # the original passes stale stack (sp50) to 0019B8C0
        expected = [e[:1] + (None,) + e[2:] if e[0] == 'object' else e for e in expected]
        log = [e[:1] + (None,) + e[2:] if e[0] == 'object' else e for e in log]
    assert expected == log, (expected, log)
    for name, offset, size, is_float in FLOOR_FIELDS:
        raw = oracle.load(base + offset, size)
        value = getattr(actor, name)
        got = bits(value) if is_float else value
        assert got == raw, (name, hex(got), hex(raw))
    for i in range(3):
        assert bits(actor.position[i]) == oracle.load(base + 0xB0 + 4 * i), ('position', i)
    assert bool(actor.link) == bool(oracle.load(base + 0x214)), 'link'
    return oracle, log


def random_floor_actor(rng):
    actor = FloorActor()
    for i in range(3):
        actor.position[i] = rng.uniform(-300, 300)
    actor.probe[0], actor.probe[1], actor.probe[2] = 0.0, rng.choice([-13.8, -13.8, -5.0]), 0.0
    actor.yaw = rng.uniform(-3.2, 3.2)
    actor.pitch = rng.choice([0.0, 0.3])
    actor.slope = rng.choice([0.0, 0.0016297102, 0.5])
    actor.surface_y = rng.uniform(-10, 300)
    actor.conveyor_yaw = rng.uniform(-3, 3); actor.slide_yaw = rng.uniform(-3, 3)
    actor.surface_class = rng.choice([0, 0x1000, 0x4000, 0x2000])
    actor.mode = rng.choice([0, 1, 2, 0x30, 0x30, 11, rng.randrange(256)])
    actor.surface_mode = rng.choice([0, 5, 0x35, 0x35, 0x39, rng.randrange(256)])
    actor.surface = rng.choice([0, 3, 5, 0x5A, 0x5B, 0x5C])
    actor.contact = rng.choice([0, 0, 0, 0, 1, 0x81])
    actor.floor_hit = rng.choice([0, 0, 1])
    actor.depth = rng.choice([0, 0, 1, 2]); actor.puddle = rng.choice([0, 0, 1]); actor.marsh = rng.choice([0, 0, 1])
    actor.lock = rng.choice([0, 2]); actor.slide = rng.choice([0, 1])
    actor.major = rng.choice([1, 1, 2, 4]); actor.state = rng.choice([0, 1, 1, 8, 0x1C, 5, rng.randrange(256)])
    actor.link = rng.choice([0, 0, 1]); actor.link_flags = rng.choice([4, 0x24, 0xE4, 2, rng.randrange(256)])
    actor.link_type = rng.choice([2, 6, 10, 12, 24, 40, 42, 7, rng.randrange(256)])
    if not actor.link: actor.contact &= 0x7F   # 0x80 is only ever set with a +214 link
    return actor


def floor_hit_for(rng, actor):
    hit = random_hit(rng, kind=rng.choice([2, 4, 4, 1, 3]))
    hit.node = rng.choice([0x4000, 0x4005, 0x4003, 0x1000, 0x1005, 0x2000, 0x2005, 0x8000, 0x4035,
                           0x1035, 0x4039, 0x1039, 0x405A, 0x405B, 0x405C, rng.randrange(65536)])
    nx, nz = rng.uniform(-1, 1), rng.uniform(-1, 1)
    hit.normal[0], hit.normal[1], hit.normal[2] = nx, rng.choice([0.0, 0.2, 0.9, 1.0, -0.5]), nz
    hit.point[1] = actor.position[1] + rng.uniform(-1, 14)
    return hit


def floor_section(elf, native, rng, result):
    outcomes = {}
    for case in range(5000):
        actor = random_floor_actor(rng)
        search = rng.choice([1, 1, 0])
        probes = []
        # The floor probe (and possibly eight search probes), then the head probe,
        # a possible 0x5B depth probe and a possible object probe.
        if rng.randrange(3):
            probes.append(floor_hit_for(rng, actor))
        else:
            misses = rng.randrange(9)
            probes += [ProbeHit() for _ in range(1 + misses)]
            if misses < 8: probes.append(floor_hit_for(rng, actor))
        head = floor_hit_for(rng, actor) if rng.randrange(2) else ProbeHit()
        if head.kind: head.node = rng.choice([0x4005, 0x405A, 0x405B, 0x405C, 0x4003, head.node])
        probes += [head] + [random_hit(rng) for _ in range(3)]
        links = [rng.choice([0, 1]) for _ in range(4)]
        at_seed = [rng.uniform(-300, 300) for _ in range(3)]
        oracle, log = floor_case(elf, native, actor, search, probes, links, at_seed)
        key = tuple(sorted(set(entry[0] for entry in log)))
        outcomes[key] = outcomes.get(key, 0) + 1
    result['floor_service_cases'] = 5000
    result['floor_service_call_sets'] = {'+'.join(k): v for k, v in outcomes.items()}
    assert any('surface39' in k for k in outcomes) and any('first_contact' in k for k in outcomes)
    assert any('link_test' in k for k in outcomes) and any('object' in k for k in outcomes)


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'wrong original executable'
    out, native = build_native()
    rng = random.Random(0x1796C0)
    result = {}

    # 1. 00179450 and 00179680 alone.
    queries = 0
    for case in range(4000):
        actor = random_fall_actor(rng); table = random_table(rng)
        if case % 4 == 0:  # aim entries straight at the actor height
            for i in range(20):
                table.height[i] = number(bits(actor.position[1]) + rng.randrange(-2, 3))
        oracle = Floor(elf); oracle.tables = [table]
        oracle.save(ACTOR + 0xB4, bits(actor.position[1])); oracle.save(ACTOR + 0x258, bits(actor.below))
        oracle.call(FLOOR_QUERY, ACTOR, ACTOR + 0xB0)
        answer = native.em_player_floor_query(C.byref(actor), C.byref(table))
        assert answer == oracle.r[2], (case, answer, oracle.r[2])
        assert bits(actor.below) == oracle.load(ACTOR + 0x258), case
        queries += 1
    for case in range(500):
        actor = random_fall_actor(rng); oracle = Floor(elf)
        for name, offset, size, is_float in FALL_FIELDS:
            value = getattr(actor, name)
            oracle.save(ACTOR + offset, bits(value) if is_float else value, size)
        oracle.call(FALL_ENTER, ACTOR); native.em_player_fall_enter(C.byref(actor))
        for name, offset, size, is_float in FALL_FIELDS:
            if not is_float: assert getattr(actor, name) == oracle.load(ACTOR + offset, size), name
    result['floor_query_cases'] = queries; result['fall_enter_cases'] = 500

    # 2. 001796C0 over random states.
    outcomes = {'locked': 0, 'contact': 0, 'slide': 0, 'settle': 0, 'no_floor': 0, 'too_low': 0,
                'step_down': 0, 'slope_fall': 0}
    for case in range(6000):
        actor = random_fall_actor(rng)
        before = (actor.lock, actor.contact, actor.state)
        below = actor.below
        table = random_table(rng)
        if case % 3 == 0:  # an entry just below: step down rather than fall
            table.count = max(table.count, 1); table.flags[table.count - 1] = 1
            table.height[table.count - 1] = actor.position[1] - rng.uniform(0, 5)
        if case % 3 == 1:  # straddle the -4.01 fall threshold
            actor.lock = actor.contact = 0; actor.drop = -1.0
            actor.position[1] = rng.choice([230.0, 100.0, 229.89])
            table.count = 1; table.flags[0] = 1
            table.height[0] = actor.position[1] + rng.choice([-4.0, -4.005, -4.01, -4.0100002, -4.02, -3.99])
        probe = random_hit(rng)
        oracle, log = fall_case(elf, native, actor, [table], [probe])
        if before[0]: outcomes['locked'] += 1
        elif before[1]: outcomes['slide' if actor.state == 0x1C and before[2] != 0x1C else 'contact'] += 1
        elif not log: outcomes['settle'] += 1
        elif len(log) == 1: outcomes['too_low' if bits(actor.below) != bits(below) else 'no_floor'] += 1
        elif actor.state == 5 and actor.lock == 2: outcomes['slope_fall'] += 1
        else: outcomes['step_down'] += 1
    assert all(outcomes.values()), outcomes
    result['fall_check_cases'] = 6000; result['fall_check_outcomes'] = outcomes

    # 3. Captured first-control actor bytes as the starting state.
    capture = json.loads((REFERENCE / 'collision_run_poll.json').read_text())
    finals = {}
    for row in capture['rows']:
        finals[row['frame']] = bytes.fromhex(row['actor_hex'])
    captured = 0
    for frame, raw in sorted(finals.items()):
        ram = bytearray(0x900000); ram[0x8102B0:0x8102B0 + len(raw)] = raw
        actor = FallActor()
        base = 0x8102B0
        for name, offset, size, is_float in FALL_FIELDS:
            value = int.from_bytes(raw[offset:offset + size], 'little')
            setattr(actor, name, number(value) if is_float else value)
        for i in range(3):
            actor.position[i] = struct.unpack_from('<f', raw, 0xA0 + 4 * i)[0]   # feet, as in the callback
            actor.probe[i] = struct.unpack_from('<f', raw, 0x280 + 4 * i)[0]
        for contact in (actor.contact, 0):
            for drop in (0.0, -0.12):
                test = FallActor.from_buffer_copy(actor); test.contact = contact; test.drop = drop
                table = FloorTable(); table.count = 1; table.flags[0] = 0x4001
                table.height[0] = test.position[1] - 0.4; table.aux[0] = 0.0
                probe = ProbeHit(); probe.kind = 4; probe.node = 0x4005
                oracle = Floor(elf, bytes(ram))
                for name, offset, size, is_float in FALL_FIELDS:
                    value = getattr(test, name)
                    oracle.save(base + offset, bits(value) if is_float else value, size)
                for i in range(3): oracle.save(base + 0xB0 + 4 * i, bits(test.position[i]))
                oracle.tables = [table]; oracle.probes = [probe]
                oracle.call(FALL_CHECK, base)
                copy = FallActor.from_buffer_copy(test)
                log = []
                workers = FallWorkers(None, COLUMN_FN(lambda _, p, o: (C.memmove(o, C.byref(table), C.sizeof(table)), 0)[1]),
                                      GROUND_FN(lambda _, p, v, m, o: (C.memmove(o, C.byref(probe), C.sizeof(probe)), probe.kind)[1]))
                assert native.em_player_fall_check(C.byref(copy), C.byref(workers)) == 0
                for name, offset, size, is_float in FALL_FIELDS:
                    got = getattr(copy, name); got = bits(got) if is_float else got
                    assert got == oracle.load(base + offset, size), ('captured', frame, name)
                assert bits(copy.position[1]) == oracle.load(base + 0xB4), ('captured y', frame)
                captured += 1
    result['captured_fall_cases'] = captured

    # 3b. 00175900 over the same captured actor bytes, as the walk tail runs
    # it: +B4 lowered by 0.4, the floor found 0.4 above, the surface record.
    captured = 0
    for frame, raw in sorted(finals.items()):
        ram = bytearray(0x900000); ram[0x8102B0:0x8102B0 + len(raw)] = raw
        base = 0x8102B0
        actor = FloorActor()
        for name, offset, size, is_float in FLOOR_FIELDS:
            value = int.from_bytes(raw[offset:offset + size], 'little')
            setattr(actor, name, number(value) if is_float else value)
        feet = struct.unpack_from('<3f', raw, 0xA0)
        for i in range(3):
            actor.position[i] = feet[i]
            actor.probe[i] = struct.unpack_from('<f', raw, 0x280 + 4 * i)[0]
        actor.position[1] = fp(feet[1] - 0.4)
        actor.contact = 0; actor.floor_hit = 0; actor.link = 0
        floor = ProbeHit(); floor.kind = 4; floor.node = 0x4000 | raw[0x23A]
        floor.normal[1] = 1.0; floor.delta[1] = fp(feet[1] - actor.position[1])
        floor.point[0], floor.point[1], floor.point[2] = feet
        head = ProbeHit(); head.kind = 4; head.node = 0x4000 | raw[0x23A]; head.normal[1] = 1.0
        head.point[0], head.point[1], head.point[2] = feet
        floor_case(elf, native, actor, 1, [floor, head], [], [0.0, 0.0, 0.0], ram=bytes(ram), base=base)
        assert actor.contact == 1 and actor.surface == raw[0x23A], (frame, actor.contact, actor.surface)
        captured += 1
    result['captured_floor_cases'] = captured

    # 4. 00175900 / 00175CF0.
    floor_section(elf, native, rng, result)

    # 5. Missing workers fault.
    actor = random_fall_actor(rng); actor.lock = 0; actor.contact = 0; actor.drop = -1.0
    assert native.em_player_fall_check(C.byref(actor), C.byref(FallWorkers())) == -1
    result['fall_fault_cases'] = 1

    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print('player floor original-instruction PASS', json.dumps(result))


if __name__ == '__main__':
    main()
