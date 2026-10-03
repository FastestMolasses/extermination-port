#!/usr/bin/env python3
"""Compare the native death decal (em_effect_001F77B0.c) with the original code.

The oracle is the bounded EE interpreter of test_effect_original_reference.py
(64-bit registers and doubleword loads and stores included). It executes the ORIGINAL
instructions of the pinned boot ELF (the user's config/SCUS_971.12): the
effect node behaviour 001F77B0 and the SDK sinf / cosf 0011E2A8 / 0011DE90
it calls, in place, over a captured RAM image. COP1 arithmetic is the
measured EE model (tools/ee_float_model.py, docs/EE_FLOAT_MODEL.md).

Workers, recorded with their argument bits on both sides: 00122BB8 (rand,
scripted results), 001CE300 (the decal kernel: tag, the 16 corner words,
the TEX0 doubleword, the colour) and 001AFC10 (free). The native sinf /
cosf workers answer with what the original routines computed for the same
argument bits, so the call order and the arguments are what is compared.

Per call: the state byte, the work block +0x1F0..+0x2B3, the scratchpad
words 0x70003A20 / 0x70003A24 and the worker calls must be equal, and every
byte the original changed must be one of those (or the stack).

Cases: every subtype 0..7 from state 0 (2..5 are the parameter sets, the
rest set nothing), then state-1 ticks past the size cap; the states 2 and 3
(free), 4 and 0xFF (nothing); random positions and listeners. Capture
evidence: the DAMAGE lane's dmg_02_flame_death end snapshot holds the
player's live death decal (record 0x007B0970, subtype 2, sizes 186 / 210);
it is run in lockstep, original against native, from its captured state
until every size has reached the cap (docs/DAMAGE.md section 4).

No original instruction bytes, disassembly or data are written by this file;
build/effect_001F77B0_reference/report.json holds only counts.
"""
import ctypes as C
import hashlib
import json
import random
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_effect_original_reference as EO  # noqa: E402
from reference_mode import banner, pick  # noqa: E402

ROOT, DECOMP, ELF_SHA = EO.ROOT, EO.DECOMP, EO.ELF_SHA
M32, M64 = EO.M32, EO.M64
OUT = ROOT / 'build/effect_001F77B0_reference'
CAPTURE = DECOMP / 'build/c10/damage/dmg_02_flame_death'
CAPTURED_NODE = 0x007B0970
NODE = 0x01E00000
NESTED_STACK = 0x01F40000
ENTRY = 0x001F77B0
W_RAND, W_SIN, W_COS, W_DECAL, W_FREE = 0x122BB8, 0x11E2A8, 0x11DE90, 0x1CE300, 0x1AFC10
LISTENER = 0x00810360
WORK, WORK_SIZE = 0x1F0, 0xC4
SPAD = (0x70003A20, 0x70003A24)

U32P = C.POINTER(C.c_uint32)
RAND = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_int32))
TRIG = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, U32P)
DECAL = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, U32P, C.c_uint64, C.c_uint32)
FREE = C.CFUNCTYPE(C.c_int, C.c_void_p)
LISTEN = C.CFUNCTYPE(C.c_int, C.c_void_p, U32P)


class Workers(C.Structure):    # EmEffect001F77B0Workers
    _fields_ = [('ctx', C.c_void_p), ('w_00122BB8', RAND), ('w_0011E2A8', TRIG), ('w_0011DE90', TRIG),
                ('w_001CE300', DECAL), ('w_001AFC10', FREE), ('r_00810360', LISTEN)]


class Node(C.Structure):       # EmEffect001F77B0Node
    _fields_ = [('state', C.POINTER(C.c_uint8)), ('subtype', C.c_uint8), ('b0', U32P), ('c0', U32P),
                ('work', C.POINTER(C.c_uint8)), ('spad3A20', U32P), ('spad3A24', U32P)]


def build_lib():
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / 'effect_001F77B0.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
                    '-fPIC', '-Isrc', 'src/game/em_effect_001F77B0.c', '-o', str(path)], cwd=ROOT, check=True)
    lib = C.CDLL(str(path))
    lib.em_effect_001F77B0.argtypes = [C.POINTER(Node), C.POINTER(Workers), U32P]
    lib.em_effect_001F77B0.restype = C.c_int
    return lib


class Case:
    """One original call against one native call over the same state."""

    def __init__(self, lib, elf, ram, spad, node):
        self.lib, self.elf, self.node = lib, elf, node
        self.ee = EO.EE(elf, ram, spad)
        self.rands = []
        self.checks = 0

    # ------------------------------------------------------------ original
    def original(self):
        ee, calls, rands = self.ee, [], iter(self.rands)

        def rand(e):
            v = next(rands)
            calls.append(('rand',))
            e.set32(2, v)

        def decal(e):
            corners = tuple(e.load(e.u32(5) + 4 * i) for i in range(16))
            calls.append(('decal', e.g32(4), corners, e.r[6] & M64, e.u32(7)))
            e.set32(2, 0)

        def free(e):
            calls.append(('free', e.u32(4)))
            e.set32(2, 0)

        ee.stubs = {W_RAND: rand, W_DECAL: decal, W_FREE: free}
        trig = []
        for name, address in (('sin', W_SIN), ('cos', W_COS)):
            def run(e, name=name, address=address):
                # the original routine in place, on a nested stack
                x = e.f[12]
                sub = EO.EE(self.elf, e.ram, e.spad)
                sub.journal = e.journal
                outer, EO.STACK = EO.STACK, NESTED_STACK
                try:
                    sub.call(address, floats=(x,))
                finally:
                    EO.STACK = outer
                e.f[0] = sub.f[0]
                calls.append((name, x))
                trig.append(((name, x), sub.f[0]))
            ee.stubs[address] = run
        before = len(ee.journal)
        ee.call(ENTRY, args=(self.node,))
        touched = {(id(buf), o + i) for buf, o, old in ee.journal[before:] for i in range(len(old))}
        return calls, dict(trig), touched

    def original_view(self):
        ee, n = self.ee, self.node
        return (ee.load(n + 4, 1), bytes(ee.ram[n + WORK:n + WORK + WORK_SIZE]),
                tuple(ee.load(a) for a in SPAD))

    # -------------------------------------------------------------- native
    def native(self, view, trig):
        state, work, spad = view['state'], view['work'], view['spad']
        n = self.node
        st = C.c_uint8(state)
        b0 = (C.c_uint32 * 4)(*[self.ee.load(n + 0xB0 + 4 * i) for i in range(4)])
        c0 = (C.c_uint32 * 4)(*[self.ee.load(n + 0xC0 + 4 * i) for i in range(4)])
        wk = (C.c_uint8 * WORK_SIZE).from_buffer_copy(work)
        s20, s24 = C.c_uint32(spad[0]), C.c_uint32(spad[1])
        calls, rands = [], iter(self.rands)
        listener = [self.ee.load(LISTENER + 4 * i) for i in range(3)]

        def w_rand(_c, out):
            out[0] = struct.unpack('<i', struct.pack('<I', next(rands)))[0]
            calls.append(('rand',))
            return 0

        def w_trig(name):
            def f(_c, x, out):
                calls.append((name, x))
                out[0] = trig[(name, x)]
                return 0
            return f

        def w_decal(_c, tag, q, tex0, rgba):
            calls.append(('decal', tag, tuple(q[i] for i in range(16)), tex0 & M64, rgba))
            return 0

        def w_free(_c):
            calls.append(('free', n))
            return 0

        def w_listen(_c, out):
            for i in range(3):
                out[i] = listener[i]
            return 0

        keep = (RAND(w_rand), TRIG(w_trig('sin')), TRIG(w_trig('cos')), DECAL(w_decal), FREE(w_free),
                LISTEN(w_listen))
        w = Workers(None, *keep)
        node = Node(C.pointer(st), self.ee.load(n + 0xD, 1), b0, c0,
                    C.cast(wk, C.POINTER(C.c_uint8)), C.pointer(s20), C.pointer(s24))
        fault = C.c_uint32(0)
        rc = self.lib.em_effect_001F77B0(C.byref(node), C.byref(w), C.byref(fault))
        assert rc == 0, ('native fault', hex(fault.value))
        return calls, (st.value, bytes(wk), (s20.value, s24.value))

    def step(self, label):
        """One call on both sides from the oracle's current state."""
        n = self.node
        view = {'state': self.ee.load(n + 4, 1), 'work': bytes(self.ee.ram[n + WORK:n + WORK + WORK_SIZE]),
                'spad': tuple(self.ee.load(a) for a in SPAD)}
        ocalls, trig, touched = self.original()
        ncalls, nview = self.native(view, trig)
        oview = self.original_view()
        assert ocalls == ncalls, (label, 'calls', ocalls[:4], ncalls[:4])
        assert oview == nview, (label, 'state / work / scratch',
                                [i for i in range(WORK_SIZE) if oview[1][i] != nview[1][i]][:8])
        # every byte the original changed is modelled (or the stack)
        ram_id, spad_id = id(self.ee.ram), id(self.ee.spad)
        modelled = {(ram_id, n + 4)} | {(ram_id, n + WORK + i) for i in range(WORK_SIZE)}
        modelled |= {(spad_id, a - 0x70000000 + i) for a in SPAD for i in range(4)}
        stack = [(EO.STACK - 0x10000, EO.STACK), (NESTED_STACK - 0x10000, NESTED_STACK)]
        stray = [hex(o) for b, o in touched - modelled
                 if not (b == ram_id and any(lo <= o < hi for lo, hi in stack))]
        assert not stray, (label, 'unmodelled writes', sorted(stray)[:8])
        self.checks += 3 + len(ocalls)
        return ocalls


def synthetic(lib, elf, counts):
    ram, spad = EO.load_beat('08_truck_crossing')
    rng = random.Random(0x1F77B0)
    cases = 0
    checks = 0
    subtypes = list(range(8)) + [0xFF]
    reps = pick(12, 2)
    for subtype in subtypes:
        for rep in range(reps):
            case = Case(lib, elf, ram, spad, NODE)
            ee = case.ee
            ee.write(NODE, bytes(0x2F0))
            ee.store(NODE + 0xD, subtype, 1)
            ee.store(NODE + 0x10, ENTRY)
            for base in (NODE + 0xB0, NODE + 0xC0):
                for i in range(3):
                    ee.store(base + 4 * i, EO.fbits(rng.uniform(-600.0, 600.0)))
                ee.store(base + 12, EO.fbits(1.0))
            for i in range(3):
                ee.store(LISTENER + 4 * i, EO.fbits(rng.uniform(-600.0, 600.0)))
            # rand() results: the extremes first, then random
            edge = [0, 0x7FFFFFFF, 0x40000000, 1]
            case.rands = [edge[(rep + k) % 4] if rep < 2 else rng.randrange(0, 0x80000000) for k in range(64)]
            ticks = 0 if subtype not in (2, 3, 4, 5) else (pick(215, 30) if subtype == 2 else 3)
            calls = case.step(f'subtype {subtype:#x} state 0')
            if subtype in (2, 3, 4, 5):
                count = {2: 4, 3: 8, 4: 8, 5: 8}[subtype]
                assert sum(c[0] == 'rand' for c in calls) == 2 * count + 1
                assert sum(c[0] == 'decal' for c in calls) == count
            for t in range(ticks):
                case.rands = []
                case.step(f'subtype {subtype:#x} tick {t + 1}')
            if subtype == 2 and ticks >= 211:
                assert all(ee.load(NODE + WORK + 0x80 + 4 * i) == EO.ONE for i in range(4)), 'size cap'
            ee.restore()
            cases += 1
            checks += case.checks
    for state in (2, 3, 4, 0xFF):
        case = Case(lib, elf, ram, spad, NODE)
        ee = case.ee
        ee.write(NODE, bytes(0x2F0))
        ee.store(NODE + 4, state, 1)
        ee.store(NODE + 0xD, 2, 1)
        calls = case.step(f'state {state:#x}')
        assert calls == ([('free', NODE)] if state in (2, 3) else []), (state, calls)
        ee.restore()
        cases += 1
        checks += case.checks
    counts['synthetic_cases'], counts['synthetic_checks'] = cases, checks
    return cases


def captured(lib, elf, counts):
    ram = bytearray((CAPTURE / 'eeMemory.bin').read_bytes())
    spad = bytearray((CAPTURE / 'scratchpad.bin').read_bytes())
    n = CAPTURED_NODE
    assert struct.unpack_from('<I', ram, n + 0x10)[0] == ENTRY and ram[n + 4] == 1 and ram[n + 0xD] == 2
    sizes = struct.unpack_from('<4I', ram, n + WORK + 0x80)
    # 186 state-1 calls since the spawn: 186 * (1 / 210) in single precision
    assert len(set(sizes)) == 1 and abs(struct.unpack('<f', struct.pack('<I', sizes[0]))[0] - 186 / 210) < 1e-5
    case = Case(lib, elf, ram, spad, n)
    ticks = 0
    while not all(case.ee.load(n + WORK + 0x80 + 4 * i) == EO.ONE for i in range(4)):
        calls = case.step(f'captured tick {ticks + 1}')
        assert [c[0] for c in calls] == ['decal'] * 4
        ticks += 1
        assert ticks < 64
    case.step('captured at the cap')
    counts['captured_ticks'], counts['captured_checks'] = ticks + 1, case.checks
    return ticks + 1


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA, 'not the pinned boot ELF'
    lib = build_lib()
    counts = {}
    cases = synthetic(lib, elf, counts)
    ticks = captured(lib, elf, counts)
    (OUT / 'report.json').write_text(json.dumps(counts, indent=1) + '\n')
    banner(f'{cases} synthetic cases ({counts["synthetic_checks"]} checks)',
           f'captured death decal {ticks} lockstep ticks ({counts["captured_checks"]} checks)')
    print('PASS test_effect_001F77B0_reference')
    return 0


if __name__ == '__main__':
    sys.exit(main())
