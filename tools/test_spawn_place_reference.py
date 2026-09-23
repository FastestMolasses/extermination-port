#!/usr/bin/env python3
"""Spawn placement (WP-3 S12a): native em_spawn_001B07C0 vs the original.

Executes the original 001B07C0 (and the 001B0250 it calls, not stubbed) from
the user's ELF over the D_0024D650 table in the ELF, and runs the native
translation (src/game/em_spawn_table.c) over the exported window
(tools/export_spawn_table.py, assets/spawn/spawn_table.emsp) from the same
starting state. The callees 001EFE00, 0015C1F0 and 001B0460 are recorded
stubs (a0, a1 and the whole modelled state at the jal, after its delay slot;
caller-saved registers clobbered; 001EFE00 returns a configured word). The
modelled state is every field 001B07C0 and 001B0250 read or write (the list
LAYOUT below): the player record fields, the progress bytes, D_00275BE0, the
scratchpad words 0x70003B40..5C and 3B8D, and the +4 byte of the objects at
player +0x1C and +0x304. A store anywhere else, or a data load outside the
modelled state, the stack and the exported table window, fails the test, so
the IO struct is proven to cover every access.

Compared per case: the ordered callee events (address, arguments, state at
the call), the final state, and the result. Where the original walk leaves the
exported window (an area without a table, a room or entry past its array),
the native must fault before any write, and the original must make a load
outside the window before its first store.

Cases: every record of every room of every area in the window, entries past
each array, a null area and out-of-range room/entry bytes; D_00275BE0
{0, 1, 2} x arg0 {0, 1}; D_00810788 {0, 1} (area 0x0B's mask); random player
fields with +0x0E byte, +0x1C and +0x304 objects present or not, 3B8D {0, 1},
pending damage zero/-0.0/nonzero, D_00810706 all low bits, C7D/C7E.
Quick mode samples the bulk; EM_TEST_FULL=1 runs every combination.

Captured checks (build/startup-reference, read-only): the exported window
equals the RAM bytes at the same addresses in the opening, handoff and
playable captures; in each, the area bytes are 0B/00/00 (entry 0), and the
fields 001B07C0(0) wrote at the New Game load and nothing has rewritten since
equal the native placement of entry 0: player +0xA0/+0xA8 (x, z), the
heading +0xC4 and +0xC0/+0xC8, +0x0E, +0x60..+0x8C, D_008106C8 (001B0250),
D_00810C60 and +0x304.

No original bytes are embedded or printed; only addresses and values.
"""
import ctypes as C
import hashlib
import itertools
from pathlib import Path
import random
import struct
import subprocess
import sys

from test_point_light_reference import Oracle, RETURN, STACK, bits, signed
import reference_mode

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'Extermination'
ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
TABLE = ROOT / 'assets/spawn/spawn_table.emsp'
CAPTURES = ['opening_ee.bin', 'handoff_ee.bin', 'playable_ee.bin']
P = 0x8102B0
OBJ1C, OBJ304 = 0x900000, 0x900100
CODE = [(0x1B0250, 0x1B02F4), (0x1B07C0, 0x1B0B50)]
STUBS = {0x1EFE00: 'w_001EFE00', 0x15C1F0: 'w_0015C1F0', 0x1B0460: 'w_001B0460'}
ARGS = {0x1EFE00: 2, 0x15C1F0: 1, 0x1B0460: 1}   # declared arguments (decomp C)
CALLER_SAVED = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25]

# The modelled state, in the order the native shim serializes EmSpawnIo.
LAYOUT = [
    (P + 0x000, 1), (P + 0x004, 1), (P + 0x005, 1), (P + 0x006, 1), (P + 0x00E, 1),
    (P + 0x01C, 4), (P + 0x060, 16), (P + 0x080, 16), (P + 0x0A0, 16), (P + 0x0B0, 16),
    (P + 0x0C0, 16), (P + 0x220, 16), (P + 0x230, 4), (P + 0x234, 1), (P + 0x235, 1),
    (P + 0x304, 4),
    (0x810700, 3), (0x275BE0, 1), (0x810710, 12), (0x810720, 12), (0x810706, 1),
    (0x810707, 1), (0x810858, 4), (0x81085C, 4), (0x810788, 1), (0x810C60, 1),
    (0x810C7D, 1), (0x810C7E, 1), (0x8106C8, 4), (0x70003B40, 32), (0x70003B8D, 1),
    (OBJ1C + 4, 1), (OBJ304 + 4, 1),
]
SNAP = sum(n for _, n in LAYOUT)
OFFSET = {}
_o = 0
for _a, _n in LAYOUT:
    for _i in range(_n):
        OFFSET[_a + _i] = _o + _i
    _o += _n


def load_window(path):
    data = path.read_bytes()
    assert data[:4] == b'EMSP', 'run tools/export_spawn_table.py'
    count = struct.unpack_from('<I', data, 8)[0]
    ranges, at = [], 16 + 8 * count
    for i in range(count):
        address, size = struct.unpack_from('<II', data, 16 + 8 * i)
        ranges.append((address, size, data[at:at + size]))
        at += size
    return data, ranges


def in_window(ranges, address, size):
    return any(a <= address and address + size <= a + n for a, n, _ in ranges)


class SpawnOracle(Oracle):
    def __init__(self, elf, ranges, config):
        self.recording = False
        super().__init__(elf)
        self.ranges = ranges
        self.config = config
        self.events = []
        self.first_store = None      # index of the first modelled store
        self.first_outside = None    # (events so far, stores so far) at the first out-of-window load
        self.stores = 0
        self.garbage = 0x5EED0000
        for callee in STUBS:
            self.calls[callee] = lambda o, c=callee: o.stub(c)

    def snapshot(self):
        return bytes(Oracle.load(self, a, 1) for a in OFFSET)

    def code(self, address):
        return any(lo <= address < hi for lo, hi in CODE)

    def load(self, address, size=4):
        if self.recording and not self.code(address):
            span = range(address, address + size)
            if all(a in OFFSET for a in span) or STACK - 0x100 <= address < STACK:
                pass
            elif in_window(self.ranges, address, size):
                pass
            elif self.first_outside is None:
                # The walk left the exported window; from here the original
                # reads whatever lies there (the native has faulted).
                self.first_outside = (len(self.events), self.stores)
        return super().load(address, size)

    def save(self, address, value, size=4):
        if self.recording:
            if all(a in OFFSET for a in range(address, address + size)):
                self.stores += 1
            elif STACK - 0x100 <= address < STACK:
                pass
            else:
                raise AssertionError(('unmodelled store', hex(address), size))
        super().save(address, value, size)

    def stub(self, callee):
        args = tuple(self.r[4 + i] & 0xFFFFFFFF if i < ARGS[callee] else 0 for i in range(2))
        self.events.append((callee, args, self.snapshot()))
        for reg in CALLER_SAVED:
            self.garbage += 1
            self.r[reg] = self.garbage
        if callee == 0x1EFE00:
            self.r[2] = self.config['effect']


def run_original(elf, ranges, snap, config):
    o = SpawnOracle(elf, ranges, config)
    for address, offset in OFFSET.items():
        o.save(address, snap[offset], 1)
    o.recording = True
    o.run(0x1B07C0, [config['arg0'] & 0xFFFFFFFF])
    o.recording = False
    return o


SHIM = r'''
#include <string.h>
#include "game/em_spawn_table.h"
#define SNAP %(snap)d
#define MAXEV 8
typedef struct { uint32_t callee, a0, a1; uint8_t snap[SNAP]; } Ev;
typedef struct { int32_t arg0, fail_at, null_at; uint32_t effect; } Cfg;
typedef struct { int32_t ret, nev, fault_code; uint32_t fault_address; uint8_t snap[SNAP]; Ev ev[MAXEV]; } Out;
typedef struct { EmSpawnIo *io; const Cfg *cfg; Out *out; uint8_t o1c, o304; } Ctx;

static void f4(uint8_t *b, size_t *o, float *f, int n, int w)
{ if (w) memcpy(f, b + *o, 4u * n); else memcpy(b + *o, f, 4u * n); *o += 4u * n; }
static void u8(uint8_t *b, size_t *o, uint8_t *v, int w) { if (w) *v = b[*o]; else b[*o] = *v; *o += 1; }
static void u32(uint8_t *b, size_t *o, uint32_t *v, int w) { if (w) memcpy(v, b + *o, 4); else memcpy(b + *o, v, 4); *o += 4; }

static void io(Ctx *c, uint8_t *b, int w)
{
    EmSpawnIo *s = c->io; EmSpawnPlayer *p = &s->player; size_t o = 0;
    uint32_t six = (uint32_t)s->d8106C8;
    u8(b, &o, &p->b000, w); u8(b, &o, &p->b004, w); u8(b, &o, &p->b005, w); u8(b, &o, &p->b006, w);
    u8(b, &o, &p->b00E, w); u32(b, &o, &p->w01C, w);
    f4(b, &o, p->f060, 4, w); f4(b, &o, p->f080, 4, w); f4(b, &o, p->f0A0, 4, w);
    f4(b, &o, p->f0B0, 4, w); f4(b, &o, p->f0C0, 4, w);
    f4(b, &o, &p->f220, 1, w); f4(b, &o, &p->f224, 1, w); f4(b, &o, &p->f228, 1, w); f4(b, &o, &p->f22C, 1, w);
    u32(b, &o, &p->w230, w); u8(b, &o, &p->b234, w); u8(b, &o, &p->b235, w); u32(b, &o, &p->w304, w);
    u8(b, &o, &s->d810700, w); u8(b, &o, &s->d810701, w); u8(b, &o, &s->d810702, w);
    u8(b, &o, &s->d275BE0, w); f4(b, &o, s->d810710, 3, w); f4(b, &o, s->d810720, 3, w);
    u8(b, &o, &s->d810706, w); u8(b, &o, &s->d810707, w);
    f4(b, &o, &s->d810858, 1, w); f4(b, &o, &s->d81085C, 1, w);
    u8(b, &o, &s->d810788, w); u8(b, &o, &s->d810C60, w); u8(b, &o, &s->d810C7D, w); u8(b, &o, &s->d810C7E, w);
    u32(b, &o, &six, w); if (w) s->d8106C8 = (int32_t)six;
    f4(b, &o, s->spad3B40, 8, w); u8(b, &o, &s->spad3B8D, w);
    u8(b, &o, &c->o1c, w); u8(b, &o, &c->o304, w);
}

static int ev(Ctx *c, uint32_t callee, uint32_t a0, uint32_t a1)
{
    int i = c->out->nev;
    if (i >= MAXEV) return -1;
    c->out->nev++;
    c->out->ev[i].callee = callee; c->out->ev[i].a0 = a0; c->out->ev[i].a1 = a1;
    io(c, c->out->ev[i].snap, 0);
    return i == c->cfg->fail_at ? -1 : 0;
}
static int w_001EFE00(void *x, uint32_t a0, uint32_t a1, uint32_t *r)
{ Ctx *c = x; if (ev(c, 0x1EFE00u, a0, a1) < 0) return -1; *r = c->cfg->effect; return 0; }
static int w_0015C1F0(void *x, uint32_t a0) { return ev(x, 0x15C1F0u, a0, 0); }
static int w_001B0460(void *x, int a0) { return ev(x, 0x1B0460u, (uint32_t)a0, 0); }
static int s_object_04(void *x, uint32_t object, uint8_t v)
{
    Ctx *c = x;
    if (object == 0x%(o1c)Xu) c->o1c = v; else if (object == 0x%(o304)Xu) c->o304 = v; else return -1;
    return 0;
}

static EmSpawnTable table;
int spawn_table_init(const uint8_t *image, size_t n) { return em_spawn_table_parse(&table, image, n); }
int spawn_shim(const Cfg *cfg, const uint8_t *snap_in, Out *out)
{
    EmSpawnIo s; memset(&s, 0xA5, sizeof s);
    memset(out, 0, sizeof *out);
    Ctx c = { &s, cfg, out, 0, 0 };
    io(&c, (uint8_t *)snap_in, 1);
    s.fault.address = 0; s.fault.code = EM_SCENE_FAULT_NONE;
    EmSpawnWorkers w = { &c, w_001EFE00, s_object_04, w_0015C1F0, w_001B0460 };
    if (cfg->null_at == 1) w.w_001EFE00 = NULL;
    if (cfg->null_at == 2) w.w_0015C1F0 = NULL;
    if (cfg->null_at == 3) w.w_001B0460 = NULL;
    out->ret = em_spawn_001B07C0(&table, &s, &w, cfg->arg0);
    out->fault_code = s.fault.code; out->fault_address = s.fault.address;
    io(&c, out->snap, 0);
    return 0;
}
'''


class Cfg(C.Structure):
    _fields_ = [('arg0', C.c_int32), ('fail_at', C.c_int32), ('null_at', C.c_int32), ('effect', C.c_uint32)]


class Ev(C.Structure):
    _fields_ = [('callee', C.c_uint32), ('a0', C.c_uint32), ('a1', C.c_uint32), ('snap', C.c_uint8 * SNAP)]


class Out(C.Structure):
    _fields_ = [('ret', C.c_int32), ('nev', C.c_int32), ('fault_code', C.c_int32),
                ('fault_address', C.c_uint32), ('snap', C.c_uint8 * SNAP), ('ev', Ev * 8)]


def build_native(image):
    out = ROOT / 'build/spawn_place_reference'
    out.mkdir(parents=True, exist_ok=True)
    source = out / 'shim.c'
    source.write_text(SHIM % {'snap': SNAP, 'o1c': OBJ1C, 'o304': OBJ304})
    lib = out / ('shim.dylib' if sys.platform == 'darwin' else 'shim.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', str(source), 'src/game/em_spawn_table.c',
                    '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    native.spawn_shim.argtypes = [C.POINTER(Cfg), C.c_char_p, C.POINTER(Out)]
    native.spawn_table_init.argtypes = [C.c_char_p, C.c_size_t]
    assert native.spawn_table_init(image, len(image)) == 0, 'native parse of the exported table'
    return native


def run_native(native, snap, config, fail_at=-1, null_at=0):
    cfg = Cfg(config['arg0'], fail_at, null_at, config['effect'])
    out = Out()
    native.spawn_shim(C.byref(cfg), bytes(snap), C.byref(out))
    events = [(out.ev[i].callee, (out.ev[i].a0, out.ev[i].a1), bytes(out.ev[i].snap))
              for i in range(out.nev)]
    return out, events


def put(snap, address, value, size=1):
    for i in range(size):
        snap[OFFSET[address + i]] = value >> (8 * i) & 0xFF


def get(snap, address, size=1):
    return sum(snap[OFFSET[address + i]] << (8 * i) for i in range(size))


def random_state(rng, area, room, entry, be0):
    snap = bytearray(rng.randrange(256) for _ in range(SNAP))
    put(snap, 0x810700, area); put(snap, 0x810701, room); put(snap, 0x810702, entry)
    put(snap, 0x275BE0, be0)
    for address, count in ((0x810710, 3), (0x810720, 3), (P + 0x060, 4), (P + 0x080, 4),
                           (P + 0x0A0, 4), (P + 0x0B0, 4), (P + 0x0C0, 4), (P + 0x220, 4),
                           (0x70003B40, 8)):
        for i in range(count):
            put(snap, address + 4 * i, bits(rng.uniform(-500, 500)), 4)
    put(snap, 0x810858, bits(rng.uniform(0, 100)), 4)
    put(snap, 0x81085C, bits(rng.uniform(0, 100)), 4)
    put(snap, P + 0x00E, rng.choice([0, 1, 2, 0xFF]))
    put(snap, P + 0x01C, rng.choice([0, OBJ1C]), 4)
    put(snap, P + 0x304, rng.choice([0, OBJ304]), 4)
    put(snap, 0x70003B8D, rng.choice([0, 1, 3]))
    put(snap, 0x810788, rng.choice([0, 1, 0xFF]))
    put(snap, 0x810C7D, rng.choice([0, 1])); put(snap, 0x810C7E, rng.choice([0, 1]))
    pending = rng.choice([(0.0, 0.0), (-0.0, 0.0), (0.0, 5.0), (15.0, 0.0), (-0.0, -0.0)])
    put(snap, P + 0x224, bits(pending[0]), 4); put(snap, P + 0x22C, bits(pending[1]), 4)
    return snap


def compare(elf, ranges, native, snap, config, label):
    o = run_original(elf, ranges, snap, config)
    out, events = run_native(native, snap, config)
    if out.ret < 0:
        # Fail-stop: the native faults before any write; the original leaves
        # the exported window before its first store.
        assert bytes(out.snap) == bytes(snap), (label, 'native wrote before its fault')
        assert o.first_outside is not None and o.first_outside[1] == 0, (label, 'original stayed in the window')
        assert o.first_outside[0] == 0 and out.nev == 0, (label, 'fault after a callee')
        assert out.fault_code == 4, (label, out.fault_code)
        return 'fault'
    assert o.first_outside is None, (label, 'original left the window; the native did not fault')
    original = [(c, a, s) for c, a, s in o.events]
    assert len(original) == len(events), (label, 'callee count', [hex(c) for c, _, _ in original],
                                          [hex(c) for c, _, _ in events])
    for k, (a, b) in enumerate(zip(original, events)):
        assert a[0] == b[0] and a[1] == b[1], (label, 'callee', k, hex(a[0]), hex(b[0]), a[1], b[1])
        if a[2] != b[2]:
            diff = [hex(addr) for addr, off in OFFSET.items() if a[2][off] != b[2][off]]
            raise AssertionError((label, 'state at callee', hex(a[0]), diff[:8]))
    final = o.snapshot()
    if final != bytes(out.snap):
        diff = [hex(addr) for addr, off in OFFSET.items() if final[off] != out.snap[off]]
        raise AssertionError((label, 'final state', diff[:8]))
    return 'ok'


def fail_stop(elf, ranges, native, snap, config, label):
    """Every callee failing (or NULL) stops at exactly the original state at
    that callee and latches the fault there."""
    o = run_original(elf, ranges, snap, config)
    null_index = {0x1EFE00: 1, 0x15C1F0: 2, 0x1B0460: 3}
    for k, (callee, _, state) in enumerate(o.events):
        for mode in ('fail', 'null'):
            out, events = run_native(native, snap, config,
                                     fail_at=k if mode == 'fail' else -1,
                                     null_at=null_index[callee] if mode == 'null' else 0)
            assert out.ret < 0 and out.fault_address == callee, (label, mode, hex(callee), out.fault_address)
            assert out.fault_code == (2 if mode == 'fail' else 1), (label, mode, out.fault_code)
            assert bytes(out.snap) == state, (label, mode, 'state at the failing callee', hex(callee))
            assert len(events) == (k + 1 if mode == 'fail' else k), (label, mode, 'calls after the fault')
    return len(o.events)


def captured_checks(native, image, ranges):
    """Entry 0 placement vs the fields the captures still hold."""
    lines = []
    ref = DECOMP / 'build/startup-reference'
    snap = bytearray(SNAP)
    put(snap, 0x810700, 0x0B)
    for i in range(4):
        put(snap, P + 0x060 + 4 * i, 0, 4); put(snap, P + 0x080 + 4 * i, 0, 4)
    put(snap, P + 0x00E, 0xFF)
    out, _ = run_native(native, snap, {'arg0': 0, 'effect': 0})
    assert out.ret == 0
    placed = bytes(out.snap)
    for name in CAPTURES:
        ram = (ref / name).read_bytes()
        for address, size, data in ranges:
            assert ram[address:address + size] == data, (name, 'table window differs', hex(address))
        assert ram[0x810700:0x810703] == b'\x0b\x00\x00', (name, 'not AREA11 entry 0')
        u32 = lambda a: int.from_bytes(ram[a:a + 4], 'little')
        want = lambda a, n=4: get(placed, a, n)
        checks = [
            ('player +0xA0 x', u32(P + 0xA0), want(P + 0xA0)),
            ('player +0xA8 z', u32(P + 0xA8), want(P + 0xA8)),
            ('player +0xC0', u32(P + 0xC0), want(P + 0xC0)),
            ('player +0xC4 heading', u32(P + 0xC4), want(P + 0xC4)),
            ('player +0xC8', u32(P + 0xC8), want(P + 0xC8)),
            ('player +0x0E', ram[P + 0x0E], want(P + 0x0E, 1)),
            ('player +0x304', u32(P + 0x304), want(P + 0x304)),
            ('D_008106C8', u32(0x8106C8), want(0x8106C8)),
            ('D_00810C60', ram[0x810C60], want(0x810C60, 1)),
        ] + [(f'player +{0x60 + 4 * i:#x}', u32(P + 0x60 + 4 * i), want(P + 0x60 + 4 * i)) for i in range(4)] \
          + [(f'player +{0x80 + 4 * i:#x}', u32(P + 0x80 + 4 * i), want(P + 0x80 + 4 * i)) for i in range(4)]
        bad = [(n, hex(a), hex(b)) for n, a, b in checks if a != b]
        assert not bad, (name, bad)
        lines.append(f'{name}: window equal, {len(checks)} placement fields equal')
    return lines


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256
    if not TABLE.exists():
        print(f'missing {TABLE}: run tools/export_spawn_table.py')
        return 1
    image, ranges = load_window(TABLE)
    exported = __import__('export_spawn_table').build_spawn_table(elf)
    assert exported == image, 'asset differs from a fresh export'
    native = build_native(image)

    # Every record of every room (array bounds from the exporter's walk), one
    # past each array (still inside the window: the next array's bytes, read
    # the same by both), and walks that leave the window: an area without a
    # table (5), the zero word 0x17, area 0xFF, a zero room pointer (area
    # 0x0B room 1) and entry 0xFF.
    export = __import__('export_spawn_table')
    areas, _ = export.walk(export.elf_reader(elf))
    starts = sorted({e for _, rooms in areas.values() for e in rooms})
    floor = min(min(t for t, _ in areas.values()), 0x24D650)
    positions = []
    for area, (_, rooms) in sorted(areas.items()):
        for room, entries in enumerate(rooms):
            i = starts.index(entries)
            end = starts[i + 1] if i + 1 < len(starts) else floor
            count = (end - entries) // 0x30
            positions += [(area, room, e) for e in range(count + 1)]
    positions += [(5, 0, 0), (0x17, 0, 0), (0xFF, 0, 0), (0x0B, 1, 0), (0x0B, 0, 0xFF)]
    cases = list(itertools.product(positions, (0, 1, 2), (0, 1)))
    keep = lambda i, c: c[0][0] == 0x0B or c[0] in positions[-5:]
    chosen = reference_mode.select(cases, 400, 0x1B07C0,
                                   axes=(lambda c: c[1], lambda c: c[2], lambda c: c[0][0]), keep=keep)
    variants = reference_mode.pick(4, 1)
    rng = random.Random(0x1B07C0)
    tally = {'ok': 0, 'fault': 0}
    for (area, room, entry), be0, arg0 in chosen:
        for v in range(variants):
            snap = random_state(rng, area, room, entry, be0)
            config = {'arg0': arg0, 'effect': rng.choice([0, 0x7A5640, 0xFFFFFFFF])}
            tally[compare(elf, ranges, native, snap, config, (area, room, entry, be0, arg0, v))] += 1

    # Branch coverage of the area-flag arms: area 0x0B entry 0 with the event
    # byte set (001B0250 mask -> & 4 and & 0x60: 001EFE00), every combination.
    arms = 0
    for be0, arg0, e788, c7d, c7e, obj1c, obj304, b3d, pend in itertools.product(
            (0, 1), (0, 1), (0, 1), (0, 1), (0, 1), (0, OBJ1C), (0, OBJ304), (0, 1),
            ((0.0, 0.0), (0.0, 1.0), (1.0, 0.0))):
        snap = random_state(rng, 0x0B, 0, 0, be0)
        put(snap, 0x810788, e788); put(snap, 0x810C7D, c7d); put(snap, 0x810C7E, c7e)
        put(snap, P + 0x01C, obj1c, 4); put(snap, P + 0x304, obj304, 4); put(snap, 0x70003B8D, b3d)
        put(snap, P + 0x00E, 1 if arg0 else 0)
        put(snap, P + 0x224, bits(pend[0]), 4); put(snap, P + 0x22C, bits(pend[1]), 4)
        config = {'arg0': arg0, 'effect': 0x7A5640}
        compare(elf, ranges, native, snap, config, ('arms', be0, arg0, e788, c7d, c7e, obj1c, obj304, b3d, pend))
        arms += 1
    # Entry 1 carries the walk-out byte 1: arg0 = 1 sets +4/+5/+6.
    snap = random_state(rng, 0x0B, 0, 1, 0)
    compare(elf, ranges, native, snap, {'arg0': 1, 'effect': 0}, 'walk-out entry 1')

    stops = 0
    for e788 in (0, 1):
        snap = random_state(rng, 0x0B, 0, 0, 0)
        put(snap, 0x810788, e788); put(snap, P + 0x304, OBJ304, 4); put(snap, P + 0x01C, OBJ1C, 4)
        put(snap, 0x70003B8D, 0)
        stops += fail_stop(elf, ranges, native, snap, {'arg0': 1, 'effect': 0x7A5640}, ('fail-stop', e788))

    lines = captured_checks(native, image, ranges)
    reference_mode.banner(
        reference_mode.part(len(chosen) * variants, len(cases) * 4, 'table cases'),
        f"{arms} flag-arm cases", f"{stops} fail-stop callees")
    for line in lines:
        print(line)
    print(f'spawn place reference: PASS ({tally["ok"]} placements, {tally["fault"]} window faults '
          f'identical to executed 001B07C0/001B0250; {len(positions)} table positions)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
