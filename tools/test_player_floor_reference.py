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
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_player_reversal_reference import Reversal, ELF_SHA256  # noqa: E402
import reference_mode  # noqa: E402
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
                ('delta', C.c_float * 3), ('normal', C.c_float * 3), ('axis', C.c_float * 3),
                ('owner', C.c_void_p)]   # the probe's 0x700031D4 (an address here)


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
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8),
                ('link_owner', C.c_void_p)]   # +214: the original address in these tests


PROBE2_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_float),
                        C.POINTER(ProbeHit))
LINK_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, C.POINTER(C.c_int))
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
            # The scripted owner address when the case names one (the native
            # side receives the same value as EmPlayerProbeHit.owner).
            entity = hit.owner or ENTITY + 0x100 * self.probe_count
            self.probe_count += 1
            self.save(0x700031D4, entity if hit.entity else 0)
            self.save(entity + 2, hit.entity_flags, 1)
            self.save(entity + 3, hit.entity_type, 1)
        else:
            self.save(0x700031D0, 0)
        self.r[2] = hit.kind


def cached_build(lib, command):
    """Run the compile `command` for `lib` unless the same command already
    built it from the same inputs: every .c it names and every header under
    src/ (hashed; the stamp sits beside the library)."""
    digest = hashlib.sha256(' '.join(command).encode())
    for arg in command:
        path = ROOT / arg if not Path(arg).is_absolute() else Path(arg)
        if arg.endswith('.c') and path.exists(): digest.update(path.read_bytes())
    for header in sorted((ROOT / 'src').rglob('*.h')): digest.update(header.read_bytes())
    stamp = Path(str(lib) + '.sha256')
    if lib.exists() and stamp.exists() and stamp.read_text() == digest.hexdigest():
        return
    subprocess.run(command, cwd=ROOT, check=True)
    stamp.write_text(digest.hexdigest())


def build_native():
    out = ROOT / 'build/player_floor_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('floor.dylib' if sys.platform == 'darwin' else 'floor.so')
    cached_build(lib, ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                       '-shared', '-fPIC', '-Isrc', 'src/game/em_player_floor.c', '-lm', '-o', str(lib)])
    native = C.CDLL(str(lib))
    native.em_player_fall_check.argtypes = [C.POINTER(FallActor), C.POINTER(FallWorkers)]
    native.em_player_floor_query.argtypes = [C.POINTER(FallActor), C.POINTER(FloorTable)]
    native.em_player_fall_enter.argtypes = [C.POINTER(FallActor)]
    native.em_player_floor_service.argtypes = [C.POINTER(FloorActor), C.c_int, C.POINTER(C.c_float),
                                               C.POINTER(FloorWorkers)]
    native.em_player_floor_link_test.argtypes = [C.c_int, C.c_uint8, C.c_uint32]
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
    # Each entity hit names its 0x700031D4 owner: one address per probe slot,
    # so the +214 store and the 00175640 argument are compared by identity.
    for i, hit in enumerate(probes):
        if hit.entity and not hit.owner:
            hit.owner = ENTITY + 0x100 * (i + 1)
    oracle = Floor(elf, ram)
    for name, offset, size, is_float in FLOOR_FIELDS:
        value = getattr(actor, name)
        oracle.save(base + offset, bits(value) if is_float else value, size)
    for i in range(3):
        oracle.save(base + 0xB0 + 4 * i, bits(actor.position[i]))
        oracle.save(base + 0x280 + 4 * i, bits(actor.probe[i]))
    oracle.save(base + 0x214, actor.link_owner or 0)
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
    def link_test(_, owner, result):
        log.append(('link_test', owner or 0)); result[0] = link_queue.pop(0); return 0
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
    expected = list(oracle.log)   # ('link_test', +214) compared by value
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
    assert (actor.link_owner or 0) == oracle.load(base + 0x214), \
        ('link owner', hex(actor.link_owner or 0), hex(oracle.load(base + 0x214)))
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
    actor.link_owner = LINK if rng.choice([0, 0, 1]) else None
    actor.link_flags = rng.choice([4, 0x24, 0xE4, 2, rng.randrange(256)])
    actor.link_type = rng.choice([2, 6, 10, 12, 24, 40, 42, 7, rng.randrange(256)])
    if not actor.link_owner: actor.contact &= 0x7F   # 0x80 is only ever set with a +214 link
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


# ------------------------------------------ the live actor's byte mirrors

class LiveActor(C.Structure):
    """EmPlayerLiveActor (em_player_floor.h): the original actor bytes."""
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', C.c_void_p), ('link_prev', C.c_void_p),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


def live_mapping_check(from_live, to_live, mirror_type, fields, rng, cases=32):
    """A native mirror's from_live/to_live against an oracle's offset table:
    `fields` is [(mirror attribute, element index or None, actor offset,
    size)], the offsets the oracle's original-instruction comparisons use.
    from_live must copy exactly those bytes; to_live must write exactly
    those bytes and leave every other actor byte untouched."""
    field_at = {}
    for attr, index, offset, size in fields:
        base = getattr(mirror_type, attr).offset + (4 * index if index is not None else 0)
        field_at[(attr, index)] = (base, offset, size)
    written = {offset + i for _, offset, size in field_at.values() for i in range(size)}
    for _ in range(cases):
        live = LiveActor()
        raw = bytes(rng.randrange(256) for _ in range(0x320))
        C.memmove(live.bytes, raw, 0x320)
        mirror = mirror_type()
        from_live(C.byref(live), C.byref(mirror))
        image = C.string_at(C.addressof(mirror), C.sizeof(mirror))
        for (attr, index), (base, offset, size) in field_at.items():
            assert image[base:base + size] == raw[offset:offset + size], ('from_live', attr, index, hex(offset))
        noise = bytes(rng.randrange(256) for _ in range(C.sizeof(mirror)))
        C.memmove(C.addressof(mirror), noise, len(noise))
        before = bytes(rng.randrange(256) for _ in range(0x320))
        C.memmove(live.bytes, before, 0x320)
        to_live(C.byref(mirror), C.byref(live))
        after = bytes(live.bytes)
        for (attr, index), (base, offset, size) in field_at.items():
            assert after[offset:offset + size] == noise[base:base + size], ('to_live', attr, index, hex(offset))
        changed = [k for k in range(0x320) if k not in written and after[k] != before[k]]
        assert not changed, ('to_live wrote outside its fields', [hex(k) for k in changed[:8]])
    return len(field_at)


def live_mirror_section(native, rng, result):
    """em_player_floor_actor_* / em_player_fall_actor_* against FLOOR_FIELDS
    and FALL_FIELDS (plus the +B0 position and +280 probe vectors)."""
    native.em_player_floor_actor_from_live.argtypes = [C.POINTER(LiveActor), C.POINTER(FloorActor)]
    native.em_player_floor_actor_to_live.argtypes = [C.POINTER(FloorActor), C.POINTER(LiveActor)]
    native.em_player_fall_actor_from_live.argtypes = [C.POINTER(LiveActor), C.POINTER(FallActor)]
    native.em_player_fall_actor_to_live.argtypes = [C.POINTER(FallActor), C.POINTER(LiveActor)]
    vectors = [('position', i, 0xB0 + 4 * i, 4) for i in range(3)] + \
              [('probe', i, 0x280 + 4 * i, 4) for i in range(3)]
    floor = [(name, None, offset, size) for name, offset, size, _ in FLOOR_FIELDS] + vectors
    fall = [(name, None, offset, size) for name, offset, size, _ in FALL_FIELDS] + vectors
    n = live_mapping_check(native.em_player_floor_actor_from_live, native.em_player_floor_actor_to_live,
                           FloorActor, floor, rng)
    m = live_mapping_check(native.em_player_fall_actor_from_live, native.em_player_fall_actor_to_live,
                           FallActor, fall, rng)
    # The +214 owner and its cached +2/+3 bytes travel beside the image.
    live, actor = LiveActor(), FloorActor()
    live.link_owner, live.link_flags, live.link_type = LINK, 0x84, 0x24
    native.em_player_floor_actor_from_live(C.byref(live), C.byref(actor))
    assert (actor.link_owner, actor.link_flags, actor.link_type) == (LINK, 0x84, 0x24)
    actor.link_owner, actor.link_flags, actor.link_type = ENTITY, 4, 6
    native.em_player_floor_actor_to_live(C.byref(actor), C.byref(live))
    assert (live.link_owner, live.link_flags, live.link_type) == (ENTITY, 4, 6)
    result['live_mirror_fields'] = {'floor': n, 'fall': m}


# ------------------------------------ the player stage (em_player_stage_*)

STATE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(LiveActor))
RATE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.POINTER(C.c_float))
ADVANCE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(LiveActor), C.c_float, C.POINTER(C.c_uint32))
RESULT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(LiveActor), C.POINTER(C.c_int))
ACTOR_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(LiveActor))
BLEND_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(LiveActor), C.c_float)
STOP_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int)
FADE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int)


class StageScene(C.Structure):
    _fields_ = [('spad3B8D', C.c_uint8), ('spad3B8F', C.c_uint8), ('area', C.c_uint8),
                ('d8106F1', C.c_uint8), ('d810CB6', C.c_uint8), ('busy', C.c_uint8)]


class StageWorkers(C.Structure):
    _fields_ = [('context', C.c_void_p), ('clip_rate', RATE_FN), ('advance', ADVANCE_FN),
                ('commit', RESULT_FN), ('reaction', RESULT_FN), ('drain', ACTOR_FN),
                ('heartbeat', ACTOR_FN), ('scripted_check', RESULT_FN),
                ('scripted_notify', ACTOR_FN), ('row_request', BLEND_FN), ('stop_sound', STOP_FN),
                ('major', STATE_FN * 7), ('major_context', C.c_void_p * 7),
                ('state', STATE_FN * 0x26), ('state_context', C.c_void_p * 0x26),
                ('state2', STATE_FN * 0x1A), ('state2_context', C.c_void_p * 0x1A),
                ('phase13', STATE_FN * 5), ('phase13_context', C.c_void_p * 5),
                ('phase14', STATE_FN * 4), ('phase14_context', C.c_void_p * 4)]


class Stage(C.Structure):
    _fields_ = [('scene', C.POINTER(StageScene)), ('workers', C.POINTER(StageWorkers))]


class StageFade(C.Structure):
    _fields_ = [('context', C.c_void_p), ('fade', FADE_FN)]


STAGE_BEGIN_ADDR, STAGE_ACTOR_ADDR = 0x15BA50, 0x15BCF0
WRAPPER1, WRAPPER2, KILL_PLANE = 0x15B130, 0x15B770, 0x15D460
ADVANCE_TIME, COMMIT = 0x1C64F0, 0x183090
MAJORS = {0: 0x15C420, 1: WRAPPER1, 2: WRAPPER2, 4: 0x15B530, 5: 0x15B610, 6: KILL_PLANE}
REACTION, DRAIN, HEARTBEAT = 0x21C440, 0x15D100, 0x15D000
SCRIPTED_CHECK, SCRIPTED_NOTIFY, ROW_REQUEST, STOP_SOUND, FADE = 0x182B30, 0x182D70, 0x174A50, 0x11A070, 0x1AEDE0
# 0015B130's per-state routines by +5 (its jump table; 0x19 = 0016DE40 outside
# the scripted takeover) and 0015B770's (0x19 = 002255C0; 0xD / 0xE by +D).
TABLE1 = (0x161020, 0x1612D0, 0x161790, 0x162190, 0x162A40, 0x162DB0, 0x1634A0, 0x1639E0,
          0x163B40, 0x1647D0, 0x1645D0, 0x165B60, 0x1662D0, 0x167C80, 0x168050, 0x169250,
          0x169730, 0x16AC50, 0x16AE40, 0x16B790, 0x16B8A0, 0x16A8B0, 0x16BC40, 0x16BF80,
          0x16D130, 0x16DE40, 0x16EBA0, 0x16EF50, 0x16C6A0, 0x16FCF0, 0x1703E0, 0x1729A0,
          0x173000, 0x1735C0, 0x173E60, 0x1741D0, 0x1747F0)
TABLE2 = {0: 0x21D800, 23: 0x21D800, 1: 0x21E240, 2: 0x21E490, 24: 0x21E490, 3: 0x21E830,
          4: 0x221FC0, 5: 0x222580, 6: 0x222AD0, 7: 0x2230A0, 9: 0x2236F0, 10: 0x223C70,
          11: 0x21F330, 12: 0x21F850, 15: 0x2202C0, 16: 0x21DBB0, 17: 0x21E9C0, 18: 0x21EAD0,
          19: 0x21EAD0, 20: 0x21EF30, 21: 0x224FE0, 22: 0x225570, 25: 0x2255C0}
PHASE13 = (0x21FB40, 0x2208C0, 0x220D30, 0x221630, 0x221C70)
PHASE14 = (0x21FED0, 0x220B50, 0x221060, 0x2217C0)
STAGE_NOOPS = (0x102948, 0x1C6DA0, 0x1C68C0, 0x1C6960, 0x15CF90, 0x15CBA0, 0x187350)


class StageOracle(Floor):
    """The original stage routines with every callee hooked and recorded;
    a hooked state routine writes the case's scripted exit bytes."""

    def __init__(self, elf, exits, results):
        super().__init__(elf)
        self.exits = exits          # offset -> byte, written by every hooked routine
        self.results = results      # scripted return values by callee address
        def routine(tag):
            def run(o):
                o.log.append(tag)
                for offset, value in o.exits.items(): o.save(ACTOR + offset, value, 1)
                o.r[2] = 0
            return run
        for n, address in MAJORS.items(): self.calls[address] = routine(('major', n))
        for i, address in enumerate(TABLE1): self.calls[address] = routine(('state', address))
        for address in set(TABLE2.values()) | set(PHASE13) | set(PHASE14):
            self.calls[address] = routine(('state', address))
        def result(tag, address):
            return lambda o: (o.log.append(tag),
                              o.r.__setitem__(2, o.results.get(address, 0) & 0xffffffff))
        self.calls[ADVANCE_TIME] = lambda o: (o.log.append(('advance', o.f[12])),
                                              o.r.__setitem__(2, o.results[ADVANCE_TIME]))
        self.calls[COMMIT] = result(('commit',), COMMIT)
        self.calls[REACTION] = result(('reaction',), REACTION)
        self.calls[SCRIPTED_CHECK] = result(('check',), SCRIPTED_CHECK)
        self.calls[DRAIN] = result(('drain',), DRAIN)
        self.calls[HEARTBEAT] = result(('heartbeat',), HEARTBEAT)
        self.calls[SCRIPTED_NOTIFY] = result(('notify',), SCRIPTED_NOTIFY)
        self.calls[ROW_REQUEST] = lambda o: (o.log.append(('row', o.f[12])), o.r.__setitem__(2, 0))
        self.calls[STOP_SOUND] = lambda o: (o.log.append(('stop', signed(o.r[4]))), o.r.__setitem__(2, 0))
        self.calls[FADE] = lambda o: (o.log.append(('fade', o.r[4], o.r[5])), o.r.__setitem__(2, 0))
        for address in STAGE_NOOPS: self.calls[address] = lambda o: o.r.__setitem__(2, 0)


class NativeStage:
    """The same callees on the native side, recording into one log."""

    def __init__(self, elf, exits, results):
        self.log = []
        self.exits, self.results = exits, results
        w = self.workers = StageWorkers()
        self.keep = []

        def keep(fn): self.keep.append(fn); return fn

        def routine(tag):
            def run(_, actor):
                self.log.append(tag)
                for offset, value in self.exits.items(): actor.contents.bytes[offset] = value
                return 0
            return keep(STATE_FN(run))

        def result(tag, address):
            def run(_, actor, out):
                self.log.append(tag); out[0] = self.results.get(address, 0); return 0
            return keep(RESULT_FN(run))

        def plain(tag):
            def run(_, actor): self.log.append(tag); return 0
            return keep(ACTOR_FN(run))

        def rate(_, clip, out):
            out[0] = struct.unpack_from('<f', elf, 0x248C98 - 0x100000 + 0x300 + clip * 12)[0]
            return 0

        def advance(_, actor, step, flags):
            self.log.append(('advance', bits(step))); flags[0] = self.results[ADVANCE_TIME]; return 0

        w.clip_rate = keep(RATE_FN(rate))
        w.advance = keep(ADVANCE_FN(advance))
        w.commit = result(('commit',), COMMIT)
        w.reaction = result(('reaction',), REACTION)
        w.scripted_check = result(('check',), SCRIPTED_CHECK)
        w.drain = plain(('drain',))
        w.heartbeat = plain(('heartbeat',))
        w.scripted_notify = plain(('notify',))
        w.row_request = keep(BLEND_FN(lambda _, a, blend: (self.log.append(('row', bits(blend))), 0)[1]))
        w.stop_sound = keep(STOP_FN(lambda _, handle: (self.log.append(('stop', handle)), 0)[1]))
        for n in MAJORS: w.major[n] = routine(('major', n))
        for i, address in enumerate(TABLE1):
            if i < 0x25: w.state[i] = routine(('state', address))
        for i, address in TABLE2.items(): w.state2[i] = routine(('state', address))
        for i, address in enumerate(PHASE13): w.phase13[i] = routine(('state', address))
        for i, address in enumerate(PHASE14): w.phase14[i] = routine(('state', address))


# Deterministic scenarios, one per case at the start of each routine's run
# (the rest are random): every branch of each routine in the default run.
def _sc(raw=None, scene=None, results=None, b4=None, event=None):
    return {'raw': raw or {}, 'scene': scene or {}, 'results': results or {}, 'b4': b4, 'event': event}


STAGE_SCENARIOS = {
    '0015BA50': [_sc({4: n, 5: 5}) for n in range(8)] +
                [_sc({4: 4, 5: 0}, results={COMMIT: 1}), _sc({4: 4, 5: 0x17}, results={COMMIT: 0}),
                 _sc({4: 2, 5: 0xD, 0x1F1: 1, 0x1F0: 0}), _sc({4: 1, 0x1F0: 0x33}),
                 _sc({4: 1, 0x1F0: 0x34}, scene={'spad3B8F': 2})],
    '0015BCF0': [_sc({4: 1, 5: 0x1C, 0x31A: 1, 0x31B: 3}, b4=10.0, event=0x12E),
                 _sc({4: 2, 5: 0x1C, 0x31A: 1, 0x31B: 3}, b4=10.0, event=0x12E),
                 _sc({4: 1, 5: 0x17, 0x31A: 1, 0x31B: 3}, b4=10.0, event=0x135),
                 _sc({4: 6, 5: 0x17, 0x31A: 1, 0x31B: 3}, b4=10.0, event=0x135),
                 _sc({4: 1, 5: 0x1E, 0x275: 4, 0x31A: 1, 0x31B: 3}, b4=10.0, event=0x5DD),
                 _sc({4: 1, 5: 0x1E, 0x275: 3, 0x31A: 1, 0x31B: 3}, b4=10.0, event=0x5DD),
                 _sc({4: 1, 5: 0x22, 0x275: 4, 0x31A: 1, 0x31B: 3}, b4=10.0, event=0x5DD),
                 _sc({4: 1, 5: 0, 0x31A: 1, 0x31B: 0xFF}, b4=10.0, event=0x12E),
                 _sc({4: 1, 5: 0}, b4=-200.01), _sc({4: 1, 5: 0}, b4=-200.0),
                 _sc({4: 2, 5: 0x16}, b4=-250.0), _sc({4: 6, 5: 1}, b4=-250.0)],
    '0015B130': [_sc({5: 0x19}, scene={'spad3B8D': 3, 'spad3B8F': 2}),
                 _sc({5: 0x19}, scene={'spad3B8D': 0, 'spad3B8F': 2}),
                 _sc({5: 5, 0x1F0: 0x2A}, scene={'spad3B8D': 3, 'area': 0xB}),
                 _sc({5: 5, 0x1F0: 0x2A}, scene={'spad3B8D': 3, 'area': 0x15},
                     results={SCRIPTED_CHECK: 1}),
                 _sc({5: 5, 0x1F0: 0x17}, scene={'spad3B8D': 3}),
                 _sc({5: 5, 0x1F0: 0}, scene={'spad3B8D': 3}, results={SCRIPTED_CHECK: 0}),
                 _sc({5: 5}, scene={'spad3B8D': 0}, results={REACTION: 1}),
                 _sc({5: 5, 0x20E: 1, 0x20F: 0}, scene={'spad3B8D': 0}, results={REACTION: 0}),
                 _sc({5: 5, 0x20E: 0, 0x20F: 0, 0: 2}, scene={'spad3B8D': 0}, results={REACTION: 0}),
                 _sc({5: 5, 0x20E: 0, 0x20F: 0, 0: 1}, scene={'spad3B8D': 0}, results={REACTION: 0}),
                 _sc({5: 0x25}, scene={'spad3B8D': 0}, results={REACTION: 0}),
                 _sc({5: 0x27}, scene={'spad3B8D': 0}, results={REACTION: 0})],
    '0015B770': [_sc({5: 0xD, 0xD: n}) for n in range(6)] +
                [_sc({5: 0xE, 0xD: n}) for n in (3, 4)] +
                [_sc({5: 8}), _sc({5: 0x19}), _sc({5: 0x1A}), _sc({5: 23}), _sc({5: 24})],
    '0015D460': [_sc({5: n}) for n in range(4)],
}


def stage_actor(rng):
    raw = bytearray(rng.randrange(256) for _ in range(0x320))
    struct.pack_into('<h', raw, 0x20C, rng.randrange(0, 459))
    struct.pack_into('<f', raw, 0x204, rng.choice([1.0, 0.75, 2.0, 3.0, rng.uniform(0.1, 4.0)]))
    struct.pack_into('<f', raw, 0x34, rng.uniform(-5, 5))
    struct.pack_into('<f', raw, 0x1F4, rng.uniform(0.5, 2.0))
    struct.pack_into('<f', raw, 0xB4, rng.choice([10.0, -199.99, -200.0, -200.01, -250.0, 230.0]))
    raw[0x1F0] = rng.choice([0x31, 0x32, 0x33, 0x34, 0x35, 0x2A, 0x17, 0x41, 0, rng.randrange(256)])
    raw[0x1F1] = rng.choice([0, 1, 2])
    struct.pack_into('<H', raw, 0x276, rng.choice([0, 0, 1, 0x8000]))
    struct.pack_into('<H', raw, 0x20E, rng.choice([0, 0, 1, 2, 0xFFFF]))
    raw[0] = rng.choice([0, 1, 2, 3])
    raw[0x31A] = rng.choice([0, 1, 1])
    raw[0x31B] = rng.choice([0xFF, 0, 3, 5])
    struct.pack_into('<H', raw, 0x31C, rng.choice([0x5DD, 0x12E, 0x135, 0x100]))
    raw[0x275] = rng.choice([4, 4, 3])
    raw[0x2F3] = rng.choice([0, 3, 4, 5])
    struct.pack_into('<I', raw, 0x214, 0)
    struct.pack_into('<I', raw, 0x308, 0)
    return raw


POINTER_WORDS = set(range(0x214, 0x218)) | set(range(0x308, 0x30C))


def stage_compare(oracle, live, where):
    image = bytes(live.bytes)
    for k in range(0x320):
        if k in POINTER_WORDS: continue
        assert image[k] == oracle.load(ACTOR + k, 1), (where, hex(k), image[k], oracle.load(ACTOR + k, 1))
    assert (live.link_owner or 0) == oracle.load(ACTOR + 0x214), (where, '+214')
    assert (live.link_prev or 0) == oracle.load(ACTOR + 0x308), (where, '+308')


def stage_section(elf, native, rng, result):
    """0015BA50 (begin / dispatch / end), 0015BCF0's writes after it, 0015B130,
    0015B770 and 0015D460, executed, against em_player_stage_*: every actor
    byte, the scene bytes and every callee call in order."""
    native.em_player_stage_begin.argtypes = [C.POINTER(LiveActor), C.POINTER(StageScene), C.POINTER(StageWorkers)]
    native.em_player_stage_dispatch.argtypes = [C.POINTER(LiveActor), C.POINTER(StageWorkers)]
    native.em_player_stage_end.argtypes = [C.POINTER(LiveActor), C.POINTER(StageScene)]
    native.em_player_stage_tail.argtypes = [C.POINTER(LiveActor), C.POINTER(StageWorkers)]
    native.em_player_stage_0015B130.argtypes = [C.c_void_p, C.POINTER(LiveActor)]
    native.em_player_stage_0015B770.argtypes = [C.c_void_p, C.POINTER(LiveActor)]
    native.em_player_stage_0015D460.argtypes = [C.c_void_p, C.POINTER(LiveActor)]
    counts = {'0015BA50': 0, '0015BCF0': 0, '0015B130': 0, '0015B770': 0, '0015D460': 0}
    samples = reference_mode.pick(3000, 150)
    paths = set()
    for case in range(samples):
        raw = stage_actor(rng)
        routine = ('0015BA50', '0015BCF0', '0015B130', '0015B770', '0015D460')[case % 5]
        scene = StageScene(rng.choice([0, 0, 3]), rng.choice([0, 1, 2, 2, 3]), rng.choice([0xB, 0xB, 0x15]),
                           rng.choice([0, 0, 1]), rng.choice([0, 0, 1]), rng.choice([0, 1]))
        exits = {}
        if rng.randrange(2):
            exits = {4: rng.choice([1, 2, 4, 6]), 5: rng.choice([0, 1, 5, 0xC, 0xD, 0x16, 0x1C]),
                     0x1F0: rng.choice([0x31, 0x35, 0x33, 0]), 0x1F1: rng.choice([0, 1]),
                     0xB: 1, 0: rng.choice([0, 2])}
        results = {ADVANCE_TIME: rng.randrange(1 << 32), COMMIT: rng.choice([0, 1]),
                   REACTION: rng.choice([0, 0, 1]), SCRIPTED_CHECK: rng.choice([0, 1])}
        if routine == '0015BA50':
            raw[4] = rng.choice([0, 1, 2, 3, 4, 5, 6, 7])
            raw[5] = rng.choice([0, 0x17, 1, 5, 0xC])
        elif routine == '0015BCF0':
            raw[4] = rng.choice([1, 1, 2, 6, 3]); raw[5] = rng.choice([0x16, 0x1C, 0x17, 0x1D, 0x1E, 0x20, 0x21, 0])
            event = struct.unpack_from('<H', raw, 0x31C)[0]
            if event in (0x12E, 0x135, 0x5DD) and rng.randrange(2):
                raw[5] = {0x12E: 0x1C, 0x135: 0x17, 0x5DD: rng.choice([0x1D, 0x1E, 0x20])}[event]
                raw[0x31A], raw[0x31B] = 1, 3
        elif routine == '0015B130':
            raw[5] = rng.choice(list(range(0x29)) + [0x19, 0x19, 5, 0x1C])
        elif routine == '0015B770':
            raw[5] = rng.choice(list(range(0x1D)) + [0xD, 0xE, 0x19]); raw[0xD] = rng.randrange(6)
        else:
            raw[5] = rng.choice([0, 1, 2, 3])
        scenarios = STAGE_SCENARIOS[routine]
        if case // 5 < len(scenarios):
            forced = scenarios[case // 5]
            for offset, value in forced['raw'].items(): raw[offset] = value
            for name, value in forced['scene'].items(): setattr(scene, name, value)
            results.update(forced['results'])
            if forced['b4'] is not None: struct.pack_into('<f', raw, 0xB4, forced['b4'])
            if forced['event'] is not None: struct.pack_into('<H', raw, 0x31C, forced['event'])
        owner = rng.choice([0, LINK])
        struct.pack_into('<I', raw, 0x214, owner)
        oracle = StageOracle(elf, exits, results)
        oracle.write(ACTOR, bytes(raw))
        oracle.save(0x70003B8D, scene.spad3B8D, 1); oracle.save(0x70003B8F, scene.spad3B8F, 1)
        oracle.save(0x810700, scene.area, 1); oracle.save(0x8106F1, scene.d8106F1, 1)
        oracle.save(0x810CB6, scene.d810CB6, 1); oracle.save(0x8106B3, scene.busy, 1)
        side = NativeStage(elf, exits, results)
        live = LiveActor(); C.memmove(live.bytes, bytes(raw), 0x320)
        live.link_owner = owner or None
        stage = Stage(C.pointer(scene), C.pointer(side.workers))
        if routine == '0015BA50':
            oracle.call(STAGE_BEGIN_ADDR, ACTOR)
            assert native.em_player_stage_begin(C.byref(live), C.byref(scene), C.byref(side.workers)) == 0
            assert native.em_player_stage_dispatch(C.byref(live), C.byref(side.workers)) == 0
            native.em_player_stage_end(C.byref(live), C.byref(scene))
            assert scene.busy == oracle.load(0x8106B3, 1), (case, 'D_008106B3')
        elif routine == '0015BCF0':
            oracle.calls[STAGE_BEGIN_ADDR] = lambda o: o.r.__setitem__(2, 0)
            oracle.call(STAGE_ACTOR_ADDR, ACTOR)
            assert native.em_player_stage_tail(C.byref(live), C.byref(side.workers)) == 0
        elif routine == '0015B130':
            oracle.call(WRAPPER1, ACTOR)
            assert native.em_player_stage_0015B130(C.addressof(stage), C.byref(live)) == 0
            assert scene.spad3B8F == oracle.load(0x70003B8F, 1), (case, '0x70003B8F')
        elif routine == '0015B770':
            oracle.call(WRAPPER2, ACTOR)
            assert native.em_player_stage_0015B770(C.addressof(stage), C.byref(live)) == 0
        else:
            fade = StageFade(None, FADE_FN(lambda _, a0, a1: (side.log.append(('fade', a0, a1)), 0)[1]))
            oracle.call(KILL_PLANE, ACTOR)
            assert native.em_player_stage_0015D460(C.addressof(fade), C.byref(live)) == 0
        want = [e for e in oracle.log]
        assert side.log == want, (case, routine, side.log, want)
        stage_compare(oracle, live, (case, routine))
        counts[routine] += 1
        # Path classes every run must reach (asserted below).
        tags = {e[0] for e in want}
        if routine == '0015BA50':
            paths.add(('major', raw[4] if raw[4] <= 6 else 7))
            if scene.busy: paths.add('busy')
            if raw[4] == 4 and 'commit' in tags: paths.add('commit')
        elif routine == '0015BCF0':
            if live.bytes[4] == 6 and raw[4] != 6: paths.add('below -200')
            event = struct.unpack_from('<H', raw, 0x31C)[0]
            owned = {0x12E: 0x1C, 0x135: 0x17}.get(event)
            if live.bytes[4] == 6 and raw[4] != 6 and struct.unpack_from('<f', raw, 0xB4)[0] >= -200.0:
                raise AssertionError('below -200 fired at or above -200')
            if 'stop' in tags: paths.add(('stop', event))
            if 'stop' in tags and owned == raw[5]: paths.add(('stop outside +4 1', event))
            if raw[0x31A] and raw[0x31B] != 0xFF and 'stop' not in tags and event in (0x12E, 0x135, 0x5DD):
                paths.add(('kept', event))
        elif routine == '0015B130':
            if 'notify' in tags: paths.add('prelude')
            if 'check' in tags and 'reaction' in tags: paths.add('prelude falls through')
            if 'reaction' in tags and 'heartbeat' not in tags: paths.add('reaction skips')
            if 'drain' in tags: paths.add('drain')
            if raw[5] == 0x19 and scene.spad3B8D: paths.add('0x19 takeover')
            if 'state' in tags: paths.add('state routine')
        elif routine == '0015B770':
            if raw[5] in (0xD, 0xE) and 'state' in tags: paths.add('phase')
            if raw[5] == 0x19: paths.add('0x19')
        else:
            if 'fade' in tags: paths.add('fade')
    need = {('major', n) for n in range(8)} | {'busy', 'commit', 'below -200', ('stop', 0x5DD),
            ('stop', 0x12E), ('stop', 0x135), ('stop outside +4 1', 0x12E), ('stop outside +4 1', 0x135),
            ('kept', 0x12E), ('kept', 0x135), ('kept', 0x5DD), 'prelude', 'prelude falls through', 'reaction skips',
            'drain', '0x19 takeover', 'state routine', 'phase', '0x19', 'fade'}
    assert need <= paths, sorted(map(str, need - paths))
    # A missing worker the routine reaches is a fault.
    live = LiveActor(); live.bytes[4] = 1
    empty = StageWorkers(); scene = StageScene()
    assert native.em_player_stage_begin(C.byref(live), C.byref(scene), C.byref(empty)) == -1
    assert native.em_player_stage_dispatch(C.byref(live), C.byref(empty)) == -1
    result['stage_cases'] = counts
    result['stage_paths'] = len(paths)


# ---------------------------------------------------- real worlds, +214

# The native floor service over the actor-collision ground worker, beside the
# bridge of tools/test_actor_collision_reference.py (reused, not copied).
FLOOR_BRIDGE = r"""
#include "game/em_player_floor.h"
typedef float (*FloorSdk)(int which, float a, float b);
typedef int (*FloorRecord)(int which, EmPlayerProbeHit *hit);
typedef int (*FloorContact)(int surface);
static FloorSdk floor_sdk;
static FloorRecord floor_record;
static FloorContact floor_contact;
void floorbridge_hooks(FloorSdk s, FloorRecord r, FloorContact c)
{
    floor_sdk = s; floor_record = r; floor_contact = c;
}
typedef struct { EmActorCollisionPlayer player; } FloorCtx;
static int fb_ground(void *c, const float *p, const float *q, unsigned m, EmPlayerProbeHit *h)
{
    return em_actor_collision_player_ground(&((FloorCtx *)c)->player, p, q, m, h);
}
static int fb_head(void *c, const float *t, const float *b, EmPlayerProbeHit *h)
{
    (void)c; (void)t; (void)b; return floor_record(0, h);
}
static int fb_object(void *c, const float *at, const float *p, unsigned m, EmPlayerProbeHit *h)
{
    (void)c; (void)at; (void)p; (void)m; return floor_record(1, h);
}
static int fb_link(void *c, const void *o, int *r) { (void)c; return em_actor_collision_player_link(NULL, o, r); }
static int fb_s39(void *c, int h) { (void)c; (void)h; return -1; }
static int fb_contact(void *c, uint8_t s) { (void)c; return floor_contact(s); }
static float fb_atan2(void *c, float y, float x) { (void)c; return floor_sdk(0, y, x); }
static float fb_cos(void *c, float x) { (void)c; return floor_sdk(1, x, 0); }
static float fb_atan(void *c, float x) { (void)c; return floor_sdk(2, x, 0); }
static float fb_sqrt(void *c, float x) { (void)c; return floor_sdk(3, x, 0); }
int floorbridge_model(Bridge *b, uint32_t addr, uint8_t model, uint32_t callback)
{
    int i = find(b, addr);
    if (i < 0) return -1;
    b->rec[i].model = model; b->rec[i].callback = callback;
    return 0;
}
int floorbridge_service(Bridge *b, uint8_t cls, EmPlayerFloorActor *a, int search, float *at,
                        uint32_t *link_addr)
{
    FloorCtx ctx = { { &b->world, { &b->stranger, cls, NULL }, NULL } };
    EmPlayerFloorWorkers w = { &ctx, fb_ground, fb_head, fb_object, fb_link, fb_s39, fb_contact,
                               fb_atan2, fb_cos, fb_atan, fb_sqrt };
    int r = em_player_floor_service(a, search, at, &w);
    *link_addr = addr_of(b, a->link_owner);
    return r;
}
"""

SDK_FLOOR = C.CFUNCTYPE(C.c_float, C.c_int, C.c_float, C.c_float)
RECORD_FN = C.CFUNCTYPE(C.c_int, C.c_int, C.POINTER(ProbeHit))
CONTACT_HOOK = C.CFUNCTYPE(C.c_int, C.c_int)
SOUND_CALLS = (0x1FBD50, 0x1FB9F0, 0x1EFD90, 0x1E8B90)
# The routines this section executes beyond the actor-collision oracle's own
# checked walkers (address, size from the decomp's splat listing); each must
# equal the pinned ELF in the captured RAM.
FLOOR_CODE = ((0x175900, 0x3EC), (0x175CF0, 0x3C4), (0x19A310, 0x130), (0x175640, 0x9C),
              (0x19B6C0, 0x104), (0x19B8C0, 0x1B4), (0x1A2AE0, 0x7DC), (0x19DF10, 0x370),
              (0x1A32C0, 0x6B4), (0x19E640, 0x2EC), (0x187DC0, 0x14), (0x11E620, 0x128),
              (0x11E398, 0x88))


def real_world_section(elf, result):
    """Standing on published owner cells in the captured AREA11 worlds: the
    original 00175900 (unhooked but for the sound/effect submissions; its
    probes, 0019A310, the SDK and 00175640 all execute) against
    em_player_floor_service over em_actor_collision_player_ground and
    em_actor_collision_player_link. The head/object probes (0019B6C0 /
    0019B8C0, untranslated) are executed originally and their results handed
    to the native side; the SDK calls of the native side execute the same
    original routines. Every floor field, the position and +214 must be
    identical, and the original must reproduce the trace's +214."""
    import reference_mode
    import test_actor_collision_reference as acr
    beats = {'05_boxes': '05_boxes', '08_truck_crossing': '07_truck_preview'}
    if any(not (acr.ROUTE / w / 'eeMemory.bin').exists() for w in beats.values()) or \
            not acr.EMCL.exists() or not acr.DISC_DIRECTORY[0].exists():
        print('player floor: real-world +214 section SKIPPED (route captures or assets missing)')
        return
    out = ROOT / 'build/player_floor_reference'
    source = out / 'floor_bridge.c'
    source.write_text(acr.BRIDGE.replace('#include <string.h>', '#include <string.h>\n#include <math.h>')
                      + FLOOR_BRIDGE)
    lib = out / ('floor_bridge.dylib' if sys.platform == 'darwin' else 'floor_bridge.so')
    cached_build(lib, ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                       '-shared', '-fPIC', '-Isrc', str(source), 'src/game/em_player_floor.c',
                       'src/game/em_actor_collision.c', 'src/game/em_collision.c',
                       'src/game/em_actor_pool.c', 'src/game/em_director_original.c',
                       'src/game/em_item_sdk_math.c', 'src/game/em_interaction_scan.c', '-lm',
                       '-o', str(lib)])
    native = C.CDLL(str(lib))
    V, U8, U16, U32, I = C.c_void_p, C.c_uint8, C.c_uint16, C.c_uint32, C.c_int
    native.bridge_new.restype = V; native.bridge_new.argtypes = [C.c_char_p, U32, C.c_char_p]
    native.bridge_actor.argtypes = [V, U32, U8, U8, U16, U16, U8]
    native.bridge_list.argtypes = [V, I, C.POINTER(U32), I, I, I]
    native.floorbridge_model.argtypes = [V, U32, U8, U32]
    native.floorbridge_hooks.argtypes = [SDK_FLOOR, RECORD_FN, CONTACT_HOOK]
    native.floorbridge_service.argtypes = [V, U8, C.POINTER(FloorActor), I, C.POINTER(C.c_float),
                                           C.POINTER(U32)]
    cases = {}
    for beat, source_beat in beats.items():
        world = acr.World(source_beat)
        acr.check_code(elf, world.ram, source_beat)
        for start, size in FLOOR_CODE:
            at = start - 0x100000 + 0x300
            assert world.ram[start:start + size] == elf[at:at + size], ('code differs', hex(start))
        trace = json.loads((acr.ROUTE / beat / 'trace.json').read_text())
        # Rows on an owner the snapshot world publishes: the others (the
        # elevator at the start of 05 is no longer drawn, so 001B17A0 no
        # longer publishes it) met a different world than the snapshot.
        rows = [r for r in trace['rows'] if int(r['ground'], 16) in world.owners]
        result.setdefault('real_world_rows_other_world', 0)
        result['real_world_rows_other_world'] += sum(
            1 for r in trace['rows'] if int(r['ground'], 16) and int(r['ground'], 16) not in world.owners)
        if beat == '08_truck_crossing':
            rows = [r for r in rows if r['f'] <= 43]   # the truck moves from f44
        rows = reference_mode.select(rows, 8 if beat == '05_boxes' else 1, zlib.crc32(beat.encode()),
                                     axes=(lambda r: r['ground'],))
        ee = acr.ee_world(elf, world)
        b, _, _, _ = acr.native_world(native, world)
        for a in world.owners:                        # 00175640 reads +3 and +0x10
            native.floorbridge_model(b, a, world.ram[a + 3], acr.u32(world.ram, a + 0x10))
        ram0, spad0 = bytes(ee.mem), bytes(ee.spad)
        sdk_ee = acr.ee_world(elf, world)
        sdk_seen = {}

        def sdk(which, x, y):
            key = (which, bits(x), bits(y))
            if key not in sdk_seen:
                entry = (0x11E620, 0x11E398, 0x11DBB8, 0x11E748)[which]
                sdk_ee.call(entry, (), (x, y) if which == 0 else (x,))
                sdk_seen[key] = sdk_ee.f[0]
            return number(sdk_seen[key])
        records = []

        def record(which, out_hit):
            name, hit = records.pop(0)
            assert name == ('head', 'object')[which], ('probe order', name, which)
            C.memmove(out_hit, C.byref(hit), C.sizeof(ProbeHit))
            return hit.kind
        native_sounds = []

        def contact(surface):
            if surface == 0x5A: native_sounds.append((0x1FBD50, 0x86)); return 0
            if surface == 0x5C: native_sounds.append((0x1FB9F0, 0xA8)); return 0
            return -1
        hooks = (SDK_FLOOR(sdk), RECORD_FN(record), CONTACT_HOOK(contact))
        native.floorbridge_hooks(*hooks)
        for row in rows:
            ee.mem[:] = ram0; ee.spad[:] = spad0
            base = acr.PLAYER
            raw = world.ram[base:base + 0x320]
            actor = FloorActor()
            for name, offset, size, is_float in FLOOR_FIELDS:
                value = int.from_bytes(raw[offset:offset + size], 'little')
                setattr(actor, name, number(value) if is_float else value)
            feet = [number(bits(v)) for v in row['pos']]
            actor.position[0], actor.position[2] = feet[0], feet[2]
            actor.position[1] = fp(feet[1] - 0.4)      # the walk tail's lowered feet
            for i in range(3):
                actor.probe[i] = number(acr.u32(world.ram, base + 0x280 + 4 * i))
            actor.major, actor.state, actor.mode = 1, 1, 1
            actor.contact = actor.floor_hit = 0; actor.link_owner = None
            for name, offset, size, is_float in FLOOR_FIELDS:
                value = getattr(actor, name)
                ee.save(base + offset, bits(value) if is_float else value, size)
            for i in range(3):
                ee.save(base + 0xB0 + 4 * i, bits(actor.position[i]))
            ee.save(base + 0x214, 0)
            sounds, records[:] = [], []
            for address in SOUND_CALLS:
                ee.hooks[address] = (lambda a: lambda e: (sounds.append((a, e.arg(1) & 0xFFFF)),
                                                          e.ret_int(-1)))(address)

            def after_probe(name):
                def fn(e, _):
                    hit = ProbeHit()
                    hit.kind = e.r[2] & 0xFFFFFFFF
                    if hit.kind:
                        rec = e.load(0x700031D0)
                        hit.node = e.load(rec + 0x1A, 2)
                        for i in range(3):
                            hit.point[i] = number(e.load(0x700031B0 + 4 * i))
                            hit.delta[i] = number(e.load(0x700031C0 + 4 * i))
                            hit.normal[i] = number(e.load(rec + 0x24 + 4 * i))
                            hit.axis[i] = number(e.load(rec + 0x34 + 4 * i))
                        entity = e.load(0x700031D4)
                        hit.entity = int(entity != 0)
                        if entity:
                            hit.entity_flags, hit.entity_type = e.load(entity + 2, 1), e.load(entity + 3, 1)
                    records.append((name, hit))
                return fn
            acr.passthrough(ee, 0x19B6C0, after=after_probe('head'))
            acr.passthrough(ee, 0x19B8C0, after=after_probe('object'))
            at = (C.c_float * 3)(*actor.position)
            ee.call(0x175900, (base, 1))
            returned = ee.r[2] & 0xFF
            for address in SOUND_CALLS + (0x19B6C0, 0x19B8C0):
                del ee.hooks[address]
            native_sounds.clear()
            link = C.c_uint32()
            got = native.floorbridge_service(b, world.ram[base + 2] & 0x1F, C.byref(actor), 1, at,
                                             C.byref(link))
            assert got == returned, ('real world', beat, row['f'], got, returned)
            assert not records, ('unconsumed probe results', beat, row['f'], records)
            for name, offset, size, is_float in FLOOR_FIELDS:
                value = getattr(actor, name)
                assert (bits(value) if is_float else value) == ee.load(base + offset, size), \
                    ('real world field', beat, row['f'], name)
            for i in range(3):
                assert bits(actor.position[i]) == ee.load(base + 0xB0 + 4 * i), ('real world position', row['f'])
            stored = ee.load(base + 0x214)
            assert link.value == stored, ('real world +214', beat, row['f'], hex(link.value), hex(stored))
            assert native_sounds == sounds, ('first-contact sounds', native_sounds, sounds)
            key = (beat, row['ground'])
            entry = cases.setdefault(key, {'rows': 0, 'trace_agrees': 0})
            entry['rows'] += 1
            entry['trace_agrees'] += stored == int(row['ground'], 16)
    for (beat, owner), entry in cases.items():
        # The crates and the truck: the original stores exactly the traced
        # owner. (Other owners are reported.)
        if owner in ('0x7a7980', '0x7a7c70', '0x7a9fb0'):
            assert entry['trace_agrees'] == entry['rows'], ('trace +214', beat, owner, entry)
    result['real_world_owner_rows'] = {f'{beat} {owner}': e for (beat, owner), e in cases.items()}


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
        actor.contact = 0; actor.floor_hit = 0; actor.link_owner = None
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

    # 4b. 00175640 itself (byte-matched in the decomp), executed, against
    # em_player_link_00175640: every type byte with the three behaviour
    # addresses it names, a non-matching behaviour and 0, and NULL.
    behaviours = (0, 0x156F30, 0x827880, 0x828700, 0x1C4820, 0x156F34)
    link_cases = 0
    for present in (0, 1):
        for kind in range(256) if present else (0,):
            for behaviour in behaviours if present else (0,):
                oracle = Floor(elf)
                del oracle.calls[LINK_TEST]          # run the original, unhooked
                oracle.save(LINK + 3, kind, 1); oracle.save(LINK + 0x10, behaviour)
                oracle.call(LINK_TEST, LINK if present else 0)
                got = native.em_player_floor_link_test(present, kind, behaviour)
                assert got == oracle.r[2], ('00175640', present, hex(kind), hex(behaviour), got, oracle.r[2])
                link_cases += 1
    result['link_test_cases'] = link_cases

    # 4c. The +214 store (docs/ACTOR_COLLISION.md section 7 item 4), case by
    # case: which probe kinds store 0x700031D4, that 00175640 receives the
    # stored (or seeded) owner, and that contact |= 0x80 follows the stored
    # value. Every field and call is compared by floor_case.
    store_cases = {'stored': 0, 'kind4_kept_out': 0, 'seeded_link_test': 0, 'stored_link_test': 0}
    for case in range(96):
        actor = random_floor_actor(rng)
        actor.mode = 1; actor.surface_mode = 0; actor.contact = 0; actor.floor_hit = 0
        seeded = case % 3 == 0
        actor.link_owner = LINK if seeded else None
        hit = floor_hit_for(rng, actor)
        hit.kind = 4 if case % 3 == 1 else 2         # 0019AB20 returns 2 or 4
        hit.entity = 1; hit.owner = ENTITY + 0x100 * (1 + case % 7)
        hit.node = (0x1000, 0x4005, 0x2000, 0x1005)[case % 4]
        hit.normal[1] = 0.9
        head = ProbeHit()
        oracle, log = floor_case(elf, native, actor, 1, [hit, head, ProbeHit()], [case & 1],
                                 [0.0, 0.0, 0.0])
        stored = oracle.load(ACTOR + 0x214)
        if hit.kind & 2:
            assert stored == hit.owner, ('store', case, hex(stored))
            store_cases['stored'] += 1
        else:
            assert stored == (LINK if seeded else 0), ('kind 4 must not store', case, hex(stored))
            store_cases['kind4_kept_out'] += 1
        tested = [e for e in log if e[0] == 'link_test']
        if tested:
            store_cases['stored_link_test' if tested[0][1] == hit.owner else 'seeded_link_test'] += 1
    assert all(store_cases.values()), store_cases
    result['owner_store_cases'] = store_cases

    # 4c'. The live actor mirrors (em_player.c's live player states).
    live_mirror_section(native, rng, result)

    # 4c''. The player stage around the state callbacks.
    stage_section(elf, native, rng, result)

    # 4d. Real captured worlds: +214 from the owners the player stands on.
    real_world_section(elf, result)

    # 5. Missing workers fault.
    actor = random_fall_actor(rng); actor.lock = 0; actor.contact = 0; actor.drop = -1.0
    assert native.em_player_fall_check(C.byref(actor), C.byref(FallWorkers())) == -1
    result['fall_fault_cases'] = 1

    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print('player floor original-instruction PASS', json.dumps(result))


if __name__ == '__main__':
    main()
