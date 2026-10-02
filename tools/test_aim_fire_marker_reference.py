#!/usr/bin/env python3
"""Original-instruction oracle for the round's impact marker 0018ABA0
(src/game/em_aim_fire_marker.c, AIM_FIRE.md section 7).

The original runs from captured AREA11 RAM (route snapshots 00..14 and the
AIM end snapshots) over a pool record the case fills (a free record of the
captured pool, its state, counters, flags and vectors chosen per case); the
native module runs over a second copy through its map / call / store host.
Compared: every store in order (address, size, bytes), every callee entry
(stack pointer, argument registers), the whole RAM, scratchpad and stack at
the end. 00102948 and 001B1470 execute their original instructions on both
sides; the effect spawns, rand, the sound, 001B17A0 and the free are scripted
(rand's value per case) and not claimed verified here. The ELF supplies
every instruction; none are embedded here.

Default run a few seconds; EM_TEST_FULL=1 runs every case.
"""
import ctypes as C
import os
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode as RM  # noqa: E402
import test_player_slide_reference as S  # noqa: E402
from test_player_fall_reference import FallEE  # noqa: E402
from test_camera_aim_reference import (Call, MAP, CALL, STORE, Host, nested, first_diff,  # noqa: E402
                                       ROUTE, AIM)

OUT = ROOT / 'build/aim-fire/marker'
ENTRY, SIZE = 0x18ABA0, 0x3A4
SIG = {0x1B1470: (0, 1), 0x102948: (2, 0), 0x1EFD90: (3, 0), 0x1EFD20: (2, 0), 0x1F00A0: (4, 0),
       0x122BB8: (0, 0), 0x1FBD50: (3, 1), 0x1B17A0: (1, 0), 0x1AFC10: (1, 0)}
REAL = {0x1B1470, 0x102948}
POOL, RECORD = 0x7A5640, 0x2F0
ELF = LIB = None
IMAGES = {}


def build(source=None):
    OUT.mkdir(parents=True, exist_ok=True)
    source = source or os.environ.get('EM_AIM_FIRE_MARKER_SOURCE', str(ROOT / 'src/game/em_aim_fire_marker.c'))
    lib = OUT / (Path(source).stem + ('.dylib' if sys.platform == 'darwin' else '.so'))
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', source, '-o', str(lib)], cwd=ROOT, check=True,
                   capture_output=True)
    dll = C.CDLL(str(lib))
    dll.em_aim_fire_marker_0018ABA0.argtypes = [C.POINTER(Host), C.c_uint32]
    return dll


class Oracle(FallEE):
    def __init__(self, ram, spad):
        super().__init__(ELF, ram, spad)
        self.events, self.active, self.in_hook, self.branches = [], False, False, set()

    def save(self, a, v, size=4):
        super().save(a, v, size)
        if self.active and not self.in_hook and (a & 0xFFFFFFFF) < 0x7F000000:
            self.events.append(('store', a & 0xFFFFFFFF, size, self.read(a, size)))

    def branch(self, w, pc):
        b = super().branch(w, pc)
        if b is not None and ENTRY <= pc < ENTRY + SIZE:
            self.branches.add((pc, b[0]))
        return b


def policy(e, fn, case):
    e.r[2] = 0
    e.f[0] = 0
    if fn in REAL:
        nested(e, fn)
    elif fn == 0x122BB8:
        e.r[2] = S.sx32(case['rand'])


def free_record(ram):
    for i in range(0x100):
        a = POOL + i * RECORD
        if ram[a] == 0:
            return a
    raise AssertionError('no free pool record in the image')


def prepare(case):
    ram, spad = IMAGES[case['image']]
    e = Oracle(ram, spad)
    node = case['node']
    e.save(node + 4, case['state'], 1)
    e.save(node + 5, case['b5'], 1)
    e.save(node + 0xA, case['bA'], 1)
    e.save(node + 0xD, case['bD'], 1)
    e.save(node + 0x2E, case['flags'], 2)
    e.save(node + 0x28, case['timer'] & 0xFFFF, 2)
    e.save(node + 0xCC, case['w'], 4)
    for i, v in enumerate(case['rot']):
        e.save(node + 0xC0 + 4 * i, S.bits(v))
    for i, v in enumerate(case['pos']):
        e.save(node + 0xB0 + 4 * i, S.bits(v))
    e.save(0x810374, S.bits(case['yaw']))
    return e


def run_case(case):
    e = prepare(case)
    n = Oracle(e.mem, e.spad)
    n.stack[:] = e.stack
    node = case['node']

    def hook(fn):
        def inner(x):
            na, nf = SIG[fn]
            x.events.append(('call', fn, x.r[29] & 0xFFFFFFFF, tuple(v & 0xFFFFFFFF for v in x.r[4:4 + na]),
                             tuple(v & 0xFFFFFFFF for v in x.f[12:12 + nf])))
            x.in_hook = True
            policy(x, fn, case)
            x.in_hook = False
        return inner
    e.hooks = {fn: hook(fn) for fn in SIG}
    e.active = True
    e.call(ENTRY, [node])
    native, errors, calls = [], [], [0]
    arrays = [(C.c_uint8 * len(buf)).from_buffer(buf) for buf in (n.mem, n.spad, n.stack)]

    def mapping(_, a, size, write):
        if a < 0x2000000:
            b, i = 0, a
        elif 0x70000000 <= a < 0x70004000:
            b, i = 1, a - 0x70000000
        elif 0x7F000000 <= a < 0x7F100000:
            b, i = 2, a - 0x7F000000
        else:
            return None
        return C.addressof(arrays[b]) + i if i + size <= len(arrays[b]) else None

    def observe(_, a, size):
        native.append(('store', a, size, n.read(a, size)))

    def worker(_, p):
        c = p.contents
        try:
            fn = c.function
            na, nf = SIG[fn]
            assert (c.na, c.nf) == (na, nf), (hex(fn), 'signature')
            native.append(('call', fn, c.sp, tuple(int(v) & 0xFFFFFFFF for v in c.a[:na]),
                           tuple(int(v) for v in c.f[:nf])))
            k = len(native)
            assert native == e.events[:k], (case['name'], 'events before callee', first_diff(native, e.events))
            calls[0] += 1
            if calls[0] == case.get('failat'):
                return -1
            n.r[29] = c.sp
            for i, v in enumerate(c.a[:na]):
                n.r[4 + i] = S.sx32(int(v) & 0xFFFFFFFF)
            for i, v in enumerate(c.f[:nf]):
                n.f[12 + i] = v
            policy(n, fn, case)
            c.v0 = n.r[2] & 0xFFFFFFFFFFFFFFFF
            c.f0 = n.f[0] & 0xFFFFFFFF
            return 0
        except Exception as ex:
            errors.append(ex)
            return -1
    callbacks = (MAP(mapping), CALL(worker), STORE(observe))
    h = Host(None, *callbacks, S.STACK_TOP, 0, 0, 0)
    status = LIB.em_aim_fire_marker_0018ABA0(C.byref(h), node)
    if errors:
        raise errors[0]
    if 'failat' in case:
        assert status == -1 and h.fault == 3 and h.fault_function == ENTRY, (case['name'], 'failure latch')
        assert native == e.events[:len(native)], (case['name'], 'failure prefix')
        return set(), len(native), calls[0]
    assert status == 0 and h.fault == 0, (case['name'], status, h.fault, hex(h.fault_address))
    assert native == e.events, (case['name'], 'events', first_diff(native, e.events))
    # the stack holds only the routine's register saves (not modelled)
    assert n.mem == e.mem and n.spad == e.spad, (case['name'], 'whole memory')
    return e.branches, len(e.events), sum(x[0] == 'call' for x in e.events)


def images():
    out = []
    for folder in (ROUTE, AIM):
        if folder.exists():
            for beat in sorted(p for p in folder.iterdir() if p.is_dir()):
                if folder == ROUTE and not RM.in_scope_beat(beat.name):
                    continue
                if (beat / 'eeMemory.bin').exists() and (beat / 'scratchpad.bin').exists():
                    out.append(str(beat))
    return out


def cases(imgs):
    out = []
    rng = random.Random(0x18ABA0)
    for k in range(RM.pick(1200, 1200)):
        image = imgs[k % len(imgs)]
        case = {'image': image, 'node': free_record(IMAGES[image][0]), 'name': f'random {k}',
                'state': rng.choice((0, 0, 1, 1, 1, 1, 2, 3, 4, 0xFF)), 'b5': rng.choice((0, 1, 1, 2, 0xFF)),
                'bA': rng.choice((0, 0, 1)), 'bD': rng.choice((0, 1, 2, 2, 3)),
                'flags': rng.choice((0, 0x10, 0x11, 0x100, 0x101, 0x200, 0x201, 0x300, 0x311, 0xFFFF)),
                'timer': rng.choice((0, 1, 4, 0xFFFF, 0x8000)),
                'w': rng.choice((0, 0x80000000, S.bits(1.0), S.bits(0.5), 1)),
                'rot': tuple(rng.uniform(-1, 1) for _ in range(3)),
                'pos': tuple(rng.uniform(-400, 400) for _ in range(4)),
                'yaw': rng.uniform(-3.2, 3.2), 'rand': rng.getrandbits(32)}
        out.append(case)
    return out


def main():
    global ELF, LIB
    start = time.monotonic()
    ELF = S.read_elf()
    LIB = build()
    ee = FallEE(ELF)
    targets = {(ee.load(pc) & 0x3FFFFFF) << 2 for pc in range(ENTRY, ENTRY + SIZE, 4) if ee.load(pc) >> 26 == 3}
    assert targets == set(SIG), (sorted(map(hex, targets ^ set(SIG))))
    imgs = images()
    if not imgs:
        raise SystemExit('no captured AREA11 images under %s or %s' % (ROUTE, AIM))
    for path in imgs:
        p = Path(path)
        IMAGES[path] = ((p / 'eeMemory.bin').read_bytes(), (p / 'scratchpad.bin').read_bytes())
        assert IMAGES[path][0][ENTRY:ENTRY + SIZE] == bytes(ee.mem[ENTRY:ENTRY + SIZE]), (path, 'code')
    all_cases = cases(imgs)
    selected = RM.select(all_cases, 1200, 0x18ABA0, axes=(lambda c: (c['state'], c['bD'], c['flags'] & 0x310),))
    RM.banner(RM.part(len(selected), len(all_cases), 'marker cases'))
    rows = RM.parallel_map(run_case, selected)
    branches = set().union(*(r[0] for r in rows))
    sites = set()
    for pc in range(ENTRY, ENTRY + SIZE, 4):
        word = ee.load(pc)
        op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
        if op in (4, 20) and rs == 0 and rt == 0:
            continue
        if ee.branch(word, pc) is not None:
            sites.add(pc)
    gaps = sorted((hex(pc), b) for pc in sites for b in (False, True) if (pc, b) not in branches)
    assert not gaps, ('branch outcomes never exercised', gaps)
    fault_cases = []
    for c, row in zip(selected, rows):
        if row[2] and len(fault_cases) < 60:
            fault_cases.extend(dict(c, failat=k) for k in range(1, row[2] + 1))
    fault_rows = RM.parallel_map(run_case, fault_cases)
    print('impact marker 0018ABA0 vs original instructions: PASS %d cases, %d ordered stores / callee entries, '
          '%d callee entries, all %d branch outcomes both ways, %d callee failure cuts (%.1fs)' % (
              len(selected), sum(r[1] for r in rows), sum(r[2] for r in rows), 2 * len(sites), len(fault_rows),
              time.monotonic() - start))


if __name__ == '__main__':
    main()
