#!/usr/bin/env python3
"""Original-instruction oracle for the gun lamp (src/game/em_aim_fire_lamp.c,
docs/AIM_FIRE.md section 10).

Ten roots execute from the user's pinned ELF: 00187780, 00187690, 001D9530,
001D91A0, 001DA290, 001DA1E0, 001D4E20, 001D4EB0, 001D4B80 and 001D4C30.
Every direct store (address, size, bytes, same-value writes included; the
roots' register-save slots excluded), every callee entry (its arguments,
64-bit, its float arguments and its stack pointer) and the results are
compared in order; the native call runs over a second complete memory image
and all RAM, scratchpad and the roots' stack frames (outside the register
saves) must be equal at exit.

Callees. The SDK vector leaves, copy_qw4, 001028B8, the render-context
writers 001D1F80 / 001D7080 / 001D4750 / 001D2090 / 001D4F30 / 001D4A90,
001C6120 and the lamp's own functions (when they are not the root) execute
their ORIGINAL instructions on both sides (the captured library bank
D_0028A56C supplies the models 0x10 / 0x11 / 0x16). Scripted contracts,
identical on both sides and not claimed verified here: 00122BB8 (rand),
001B0070 (flags), 0019A570 (the probe: hit or not, the point at 0x700031B0,
the face record at 0x700031D0), 0011E748 (the VU square-root model),
0011DF78 (fabs), 0021B9A0, float_to_int 001281C0 (truncation), 00128250 (a
scripted v0, including values above 0x7FFFFFFF and the 0xFFFFFFFF of a lane
at 2^32 or above), 001CCF70, 001CFA60, 001CFBE0 and 001CB760 (recorded).

Memory: AREA11 route captures (the render context, its channel-3 cursor in
the arena, the template banks, the library) and the AIM capture aim_08's
end snapshot, with the bone table D_00275B40, the bone's rows, the node's
+0xB0, the eye D_008105D0 and the area key patched per case.

Also: every conditional branch of the ten roots both ways (the mode / variant
switches' outcomes for values other than 0 and 1 excepted: 00188ED0 passes
0 or 1, and the translation faults on any other value, checked); a callee
failure at every callee entry latches the fault with no later effect; a
missing map / call or a latched host refuses. EM_TEST_FULL=1 runs every case.
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
import reference_mode as RM
import test_player_slide_reference as S
from test_player_fall_reference import FallEE
import ee_float_model as M

OUT = ROOT / 'build/aim-fire/lamp'
FUNCS = {0x187780: 0x53C, 0x187690: 0xEC, 0x1D9530: 0x1EC, 0x1D91A0: 0x388, 0x1DA290: 0x74, 0x1DA1E0: 0xA4,
         0x1D4E20: 0x80, 0x1D4EB0: 0x80, 0x1D4B80: 0x98, 0x1D4C30: 0x98}
# Register-save area of each root (sp-relative, excluded from the stores).
SAVES = {0x187780: 0x90, 0x187690: 0x30, 0x1D9530: 0x70, 0x1D91A0: 0x60, 0x1DA290: 0x30, 0x1DA1E0: 0,
         0x1D4E20: 0x20, 0x1D4EB0: 0x20, 0x1D4B80: 0x30, 0x1D4C30: 0x30}
FRAME = {0x187780: 0x160, 0x187690: 0xA0, 0x1D9530: 0xA0, 0x1D91A0: 0x160, 0x1DA290: 0x70, 0x1DA1E0: 0,
         0x1D4E20: 0x20, 0x1D4EB0: 0x20, 0x1D4B80: 0x30, 0x1D4C30: 0x30}
SIG = {0x122BB8: (0, 0), 0x1029C0: (1, 0), 0x102948: (2, 0), 0x102BB0: (2, 1), 0x1026D0: (3, 0),
       0x102918: (3, 0), 0x1026A0: (3, 0), 0x19A570: (4, 0), 0x1028D0: (3, 0), 0x11E748: (0, 1),
       0x102900: (2, 1), 0x102760: (2, 0), 0x102738: (2, 0), 0x11DF78: (0, 1), 0x103230: (2, 1),
       0x21B9A0: (1, 2), 0x1281C0: (0, 1), 0x187690: (5, 3), 0x1B0070: (0, 0), 0x1D9530: (4, 1),
       0x1CCF70: (1, 0), 0x1CFA60: (2, 2), 0x1CFBE0: (5, 0), 0x1C6120: (2, 0), 0x1DA290: (2, 0),
       0x1D91A0: (4, 1), 0x1CB760: (4, 0), 0x1028B8: (3, 0), 0x102958: (2, 0), 0x128250: (0, 1),
       0x1D1F80: (3, 0), 0x1D4E20: (1, 0), 0x1D4EB0: (1, 0), 0x1D7080: (2, 1), 0x1D4F30: (2, 0),
       0x1D4B80: (2, 0), 0x1D4C30: (2, 0), 0x1DA1E0: (3, 0), 0x1D4750: (1, 0), 0x1D2090: (2, 0),
       0x1D4A90: (2, 0)}
SCRIPTED = {0x122BB8, 0x1B0070, 0x19A570, 0x11E748, 0x11DF78, 0x21B9A0, 0x1281C0, 0x128250, 0x1CCF70,
            0x1CFA60, 0x1CFBE0, 0x1CB760}
ROUTE = ROOT.parent / 'Extermination/build/s87/route'
AIM08 = ROOT.parent / 'Extermination/build/aimfire/capture/aim_08_light_holster'
TABLE, BONE, NODE, FACE, ROWS, COLOUR, MATRIX = (0x1E00000, 0x1E00100, 0x1E00400, 0x1E00800, 0x1E00A00,
                                                 0x1E00B00, 0x1E00C00)
MASK64 = 0xFFFFFFFFFFFFFFFF
U32, U64, I, P = C.c_uint32, C.c_uint64, C.c_int, C.POINTER


class Call(C.Structure):
    _fields_ = [('function', U32), ('sp', U32), ('a', U64 * 7), ('f', U32 * 8), ('na', U32), ('nf', U32),
                ('v0', U64), ('f0', U32)]


MAP = C.CFUNCTYPE(C.c_void_p, C.c_void_p, U32, U32, I)
CALL = C.CFUNCTYPE(I, C.c_void_p, P(Call))
STORE = C.CFUNCTYPE(None, C.c_void_p, U32, U32)


class Host(C.Structure):
    _fields_ = [('context', C.c_void_p), ('map', MAP), ('call', CALL), ('store', STORE), ('sp', U32),
                ('fault', I), ('fault_function', U32), ('fault_address', U32)]


ELF = None
LIB = None
IMAGES = {}


def build(source=None):
    OUT.mkdir(parents=True, exist_ok=True)
    source = source or os.environ.get('EM_AIM_FIRE_LAMP_SOURCE', str(ROOT / 'src/game/em_aim_fire_lamp.c'))
    lib = OUT / (Path(source).stem + ('.dylib' if sys.platform == 'darwin' else '.so'))
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', source, '-o', str(lib)], cwd=ROOT, check=True, capture_output=True)
    dll = C.CDLL(str(lib))
    h = P(Host)
    dll.em_aim_fire_lamp_00187780.argtypes = [h, U32, I, I]
    dll.em_aim_fire_lamp_00187690.argtypes = [h, U32, U32, U32, I, U64, U32, U32, U32]
    dll.em_aim_fire_lamp_001D9530.argtypes = [h, U32]
    dll.em_aim_fire_lamp_001D91A0.argtypes = [h, U32, U32, U32, I, U32]
    dll.em_aim_fire_lamp_001DA290.argtypes = [h, I, I]
    dll.em_aim_fire_lamp_001DA1E0.argtypes = [h, I, U32, I, P(U32)]
    for fn in ('001D4E20', '001D4EB0'):
        getattr(dll, 'em_aim_fire_lamp_' + fn).argtypes = [h, I]
    for fn in ('001D4B80', '001D4C30'):
        getattr(dll, 'em_aim_fire_lamp_' + fn).argtypes = [h, I, U32]
    return dll


class Oracle(FallEE):
    def __init__(self, ram=None, spad=None):
        super().__init__(ELF, ram, spad)
        self.events = []
        self.active = False
        self.in_hook = False
        self.branches = set()
        self.entry = 0
        self.sp0 = S.STACK_TOP

    def save(self, a, v, size=4):
        super().save(a, v, size)
        if self.active and not self.in_hook:
            sp = self.sp0 - FRAME[self.entry]
            if not (sp <= a < sp + SAVES[self.entry]):
                self.events.append(('store', a, size, self.read(a, size)))

    def branch(self, w, pc):
        b = super().branch(w, pc)
        if b is not None and self.entry <= pc < self.entry + FUNCS[self.entry] and self.active and \
                not self.in_hook:
            self.branches.add((pc, b[0]))
        return b


def script(e, fn, case, state):
    """The scripted contracts (both sides run them on their own memory)."""
    f = e.f[12:15]
    e.r[2] = 0
    e.f[0] = 0
    if fn == 0x122BB8:
        state['rand'] = state.get('rand', 0) + 1
        e.r[2] = S.sx32((case.get('random', 0x2468ACE) * state['rand']) & 0x7FFFFFFF)
    elif fn == 0x1B0070:
        e.r[2] = S.sx32(case.get('flags', 0))
    elif fn == 0x19A570:
        hit = case.get('hit', 1)
        e.r[2] = hit
        if hit:
            for i, n in enumerate(case.get('point', (12.5, -3.25, 40.0, 1.0))):
                e.save(0x700031B0 + 4 * i, S.bits(n))
            e.save(0x700031D0, FACE)
    elif fn == 0x11E748:
        e.f[0] = M.vu_sqrt(f[0])
        if case.get('sqrt') is not None:
            e.f[0] = S.bits(case['sqrt'])
    elif fn == 0x11DF78:
        e.f[0] = f[0] & 0x7FFFFFFF
    elif fn == 0x1281C0:
        x = struct.unpack('<f', struct.pack('<I', f[0]))[0]
        e.r[2] = S.sx32(int(x) if abs(x) < 2 ** 31 else 0x7FFFFFFF)
    elif fn == 0x128250:
        state['soft'] = state.get('soft', 0) + 1
        soft = case.get('soft')
        if soft:
            e.r[2] = soft[(state['soft'] - 1) % len(soft)] & MASK64
        else:
            x = struct.unpack('<f', struct.pack('<I', f[0]))[0]
            e.r[2] = int(x) if 0 < x < 2 ** 32 else (0xFFFFFFFF if x >= 2 ** 32 else 0)
    elif fn == 0x1CCF70:
        e.r[2] = case.get('key', 0x123)
    elif fn in (0x21B9A0, 0x1CFA60, 0x1CFBE0, 0x1CB760):
        pass
    else:
        raise AssertionError(hex(fn))


def inner_hooks(case, state):
    def hook(fn):
        def inner(x):
            script(x, fn, case, state)
        return inner
    return {fn: hook(fn) for fn in SCRIPTED}


def real(e, fn, case, state):
    """The callee's original instructions from the current registers (its
    own scripted callees answer as their contracts)."""
    saved = (list(e.r), list(e.rh), list(e.f), e.hi, e.lo, e.acc, e.cond, [list(v) for v in e.vf], list(e.vacc), e.q)
    hooks, e.hooks = e.hooks, inner_hooks(case, state)
    e.r[31] = S.RETURN
    e.r[29] = e.r[29] & 0xFFFFFFFF
    e.run(fn)
    v, f0 = e.r[2], e.f[0]
    e.r, e.rh, e.f, e.hi, e.lo, e.acc, e.cond, e.vf, e.vacc, e.q = saved
    e.hooks = hooks
    e.r[2] = v
    e.f[0] = f0


def policy(e, fn, case, state):
    if fn in SCRIPTED:
        script(e, fn, case, state)
    else:
        real(e, fn, case, state)


def library(e):
    bank = e.load(0x28A56C)
    out = []
    for m in (0x10, 0x11, 0x16):
        v, _ = e.nested(0x1C6120, [bank, m])
        out.append(v & 0xFFFFFFFF)
    return out


def f32(x):
    return S.bits(x)


def prepare(case):
    ram, spad = IMAGES[case['capture']]
    e = Oracle(ram, spad)
    e.entry = case['fn']
    rng = random.Random(case.get('seed', 1))
    # the bone table and bone, the node, the face record, the eye, the area
    e.save(TABLE, BONE)
    rows = case.get('rows') or [[rng.uniform(-1, 1) for _ in range(3)] + [0.0] for _ in range(2)] + \
        [[rng.uniform(-1, 1) for _ in range(3)] + [0.0]]
    for r_, row in enumerate(rows):
        for c, v in enumerate(row):
            e.save(BONE + 0x90 + 0x10 * r_ + 4 * c, f32(v))
    e.save(0x275B40, TABLE)
    for i, v in enumerate(case.get('pos', (371.5, 205.0, 361.3, 1.0))):
        e.save(NODE + 0xB0 + 4 * i, f32(v))
    for i, v in enumerate(case.get('normal', (0.0, 1.0, 0.0, 0.0))):
        e.save(FACE + 0x24 + 4 * i, f32(v))
    if 'eye' in case:
        for i, v in enumerate(case['eye']):
            e.save(0x8105D0 + 4 * i, f32(v))
    if 'area' in case:
        e.save(0x810700, case['area'] >> 8, 1)
        e.save(0x810701, case['area'] & 0xFF, 1)
    for i in range(4):
        e.save(ROWS + 4 * i, f32(rng.uniform(-50, 50)))
    for i in range(16):
        e.save(ROWS + 0x10 + 4 * i, f32(rng.uniform(-50, 50)))
    for i, v in enumerate(case.get('colour', (rng.uniform(0, 255), rng.uniform(0, 255), rng.uniform(0, 255),
                                              rng.uniform(0, 128)))):
        e.save(COLOUR + 4 * i, f32(v))
    for i in range(16):
        e.save(MATRIX + 4 * i, f32(rng.uniform(-2, 2) if i % 5 else rng.uniform(0.5, 2)))
    for i in range(3):
        e.save(MATRIX + 0x30 + 4 * i, f32(rng.uniform(300, 400)))
    e.save(MATRIX + 0x3C, f32(1.0))
    return e


def args_of(case, e):
    fn = case['fn']
    models = library(e)
    if fn == 0x187780:
        return [NODE, case.get('a1', 1), case.get('a2', 0)], []
    if fn == 0x187690:
        return [MATRIX, ROWS, ROWS + 0x10, case.get('size', 64), case.get('word', 0x20045D05554221F6)], \
            [f32(case.get('f12', 17.0)), f32(0.1), f32(case.get('f14', 3.5))]
    if fn == 0x1D9530:
        return [MATRIX, ROWS, ROWS + 0x10, ROWS + 0x20], [f32(case.get('f12', 250.0))]
    if fn == 0x1D91A0:
        return [MATRIX, COLOUR, models[case.get('model', 0)], case.get('flag', 0)], [f32(case.get('scale', 120.0))]
    if fn == 0x1DA290:
        return [case.get('chan', 3), case.get('a1', 0)], []
    if fn == 0x1DA1E0:
        return [case.get('chan', 3), ROWS, case.get('a2', 0)], []
    if fn in (0x1D4E20, 0x1D4EB0):
        return [case.get('chan', 3)], []
    return [case.get('chan', 3), models[case.get('model', 0)]], []


def native(fn, h, args, fargs, out):
    L = LIB
    if fn == 0x187780:
        return L.em_aim_fire_lamp_00187780(h, args[0], args[1], args[2])
    if fn == 0x187690:
        return L.em_aim_fire_lamp_00187690(h, args[0], args[1], args[2], args[3], args[4], *fargs)
    if fn == 0x1D9530:
        return L.em_aim_fire_lamp_001D9530(h, args[0])
    if fn == 0x1D91A0:
        return L.em_aim_fire_lamp_001D91A0(h, args[0], args[1], args[2], args[3], fargs[0])
    if fn == 0x1DA290:
        return L.em_aim_fire_lamp_001DA290(h, args[0], args[1])
    if fn == 0x1DA1E0:
        return L.em_aim_fire_lamp_001DA1E0(h, args[0], args[1], args[2], C.byref(out))
    if fn in (0x1D4E20, 0x1D4EB0):
        return getattr(L, f'em_aim_fire_lamp_{fn:08X}')(h, args[0])
    return getattr(L, f'em_aim_fire_lamp_{fn:08X}')(h, args[0], args[1])


def run_case(case):
    e = prepare(case)
    fn = case['fn']
    args, fargs = args_of(case, e)
    n = Oracle(e.mem, e.spad)
    n.stack[:] = e.stack
    n.entry = fn
    ostate, nstate = {}, {}

    def hook(f):
        def inner(x):
            na, nf = SIG[f]
            x.events.append(('call', f, x.r[29] & 0xFFFFFFFF, tuple(v & MASK64 for v in x.r[4:4 + na]),
                             tuple(x.f[12:12 + nf])))
            x.in_hook = True
            policy(x, f, case, ostate)
            x.in_hook = False
        return inner
    e.hooks = {f: hook(f) for f in SIG if f != fn}
    e.active = True
    for i, v in enumerate(args):
        e.r[4 + i] = v & MASK64 if i == 4 else S.sx32(v)
    for i, v in enumerate(fargs):
        e.f[12 + i] = v
    e.r[31] = S.RETURN
    e.run(fn)
    e.active = False
    native_events, errors, calls_seen = [], [], [0]
    arrays = [(C.c_uint8 * len(buf)).from_buffer(buf) for buf in (n.mem, n.spad, n.stack)]

    def mapping(_, a, size, write):
        try:
            if a < 0x2000000:
                b, i = 0, a
            elif 0x70000000 <= a < 0x70004000:
                b, i = 1, a - 0x70000000
            elif 0x7F000000 <= a < 0x7F100000:
                b, i = 2, a - 0x7F000000
            else:
                return None
            if i + size > len(arrays[b]):
                return None
            return C.addressof(arrays[b]) + i
        except Exception as ex:
            errors.append(ex)
            return None

    def observe(_, a, size):
        for at in range(a, a + size, 8):
            count = min(8, a + size - at)
            native_events.append(('store', at, count, n.read(at, count)))

    def worker(_, p):
        c = p.contents
        try:
            f = c.function
            na, nf = SIG[f]
            assert (c.na, c.nf) == (na, nf), (hex(f), 'signature', c.na, c.nf)
            native_events.append(('call', f, c.sp, tuple(c.a[:na]), tuple(c.f[:nf])))
            k = len(native_events) - 1
            assert native_events == normal(e.events)[:len(native_events)], \
                (case, 'event', k, native_events[k], normal(e.events)[k] if k < len(normal(e.events)) else 'extra')
            calls_seen[0] += 1
            if calls_seen[0] == case.get('failat'):
                return -1
            n.r[29] = c.sp
            for i, v in enumerate(c.a[:na]):
                n.r[4 + i] = v
            for i, v in enumerate(c.f[:nf]):
                n.f[12 + i] = v
            policy(n, f, case, nstate)
            c.v0 = n.r[2] & MASK64
            c.f0 = n.f[0] & 0xFFFFFFFF
            return 0
        except Exception as ex:
            errors.append(ex)
            return -1
    callbacks = (MAP(mapping), CALL(worker), STORE(observe))
    h = Host(None, *callbacks, S.STACK_TOP, 0, 0, 0)
    out = U32(0xDEADBEEF)
    status = native(fn, C.byref(h), args, fargs, out)
    if errors:
        raise errors[0]
    want = normal(e.events)
    if 'failat' in case:
        assert status == -1 and h.fault == 3 and h.fault_function == fn, (case, 'failure latch', h.fault)
        assert native_events == want[:len(native_events)], (case, 'failure prefix')
        before = list(native_events)
        assert native(fn, C.byref(h), args, fargs, out) == -1
        assert native_events == before, (case, 'latched invocation had effects')
        return set(), len(native_events), calls_seen[0]
    assert status == 0 and h.fault == 0, (case, status, h.fault, hex(h.fault_function), hex(h.fault_address))
    assert native_events == want, (case, 'final events',
                                   next((i for i, (a, b) in enumerate(zip(native_events, want)) if a != b), None),
                                   len(native_events), len(want))
    assert n.mem == e.mem and n.spad == e.spad, (case, 'whole memory')
    sp = S.STACK_TOP - FRAME[fn]
    lo, hi = sp + SAVES[fn] - 0x7F000000, S.STACK_TOP - 0x7F000000
    assert n.stack[lo:hi] == e.stack[lo:hi], (case, 'the frame outside the register saves')
    if fn == 0x1DA1E0:
        assert out.value == e.r[2] & 0xFFFFFFFF, (case, 'return', hex(out.value), hex(e.r[2]))
    return e.branches, len(e.events), sum(x[0] == 'call' for x in e.events)


def normal(events):
    """The oracle's stores split into 8-byte pieces as the native observer
    reports them (a quadword store is two)."""
    out = []
    for ev in events:
        if ev[0] == 'store' and ev[2] > 8:
            for at in range(0, ev[2], 8):
                out.append(('store', ev[1] + at, 8, ev[3][at:at + 8]))
        else:
            out.append(ev)
    return out


def captures():
    out = [str(p) for p in sorted(ROUTE.glob('*')) if RM.in_scope_beat(p.name) and (p / 'eeMemory.bin').exists()]
    if (AIM08 / 'eeMemory.bin').exists():
        out.append(str(AIM08))
    return out


def cases():
    caps = captures()
    base = caps[min(8, len(caps) - 1)]
    out = []
    for a2 in (0, 1):
        for a1 in (0, 1):
            for hit in (0, 1):
                out.append({'fn': 0x187780, 'name': 'baseline', 'capture': base, 'a1': a1, 'a2': a2, 'hit': hit,
                            'eye': (300.0, 230.0, 300.0, 1.0)})
    for field, values in {'sqrt': [0.0, 16.999998, 17.0, 17.000002, 120.0, 249.99998, 250.0, 260.0],
                          'flags': [0x20000000, 0x80, 0x200000A0],
                          'eye': [(371.5, 205.0, 361.3, 1.0), (380.0, 205.0, 361.3, 1.0), (371.5, 400.0, 361.3, 1.0)],
                          'random': [0, 0x7FFFFFFF, 1]}.items():
        for v in values:
            for a2 in (0, 1):
                out.append({'fn': 0x187780, 'name': field, 'capture': base, 'a1': 1, 'a2': a2, field: v,
                            'eye': case_eye(field, v)})
    rng = random.Random(97112)
    for k in range(60):
        out.append({'fn': 0x187780, 'name': 'random', 'capture': rng.choice(caps), 'seed': k,
                    'a1': rng.randrange(2), 'a2': rng.randrange(2), 'hit': rng.randrange(2),
                    'flags': rng.choice((0, 0x80, 0x20000000)), 'random': rng.getrandbits(31),
                    'eye': (rng.uniform(250, 450), rng.uniform(150, 300), rng.uniform(250, 450), 1.0),
                    'point': (rng.uniform(300, 450), rng.uniform(150, 300), rng.uniform(300, 450), 1.0),
                    'pos': (rng.uniform(300, 450), rng.uniform(150, 300), rng.uniform(300, 450), 1.0)})
    for k in range(8):
        out.append({'fn': 0x187690, 'name': 'flare', 'capture': rng.choice(caps), 'seed': 100 + k,
                    'size': rng.choice((64, 32, 7, 0)), 'word': rng.getrandbits(64), 'f12': rng.uniform(0, 17),
                    'f14': rng.uniform(0, 3.5)})
    for area in (0x0B00, 0x1200, 0x0201, 0x0300, 0x1000, 0x0202):
        for flags in (0, 0x80):
            out.append({'fn': 0x1D9530, 'name': 'cone', 'capture': base, 'area': area, 'flags': flags,
                        'seed': area + flags})
    for flag in (0, 1):
        for model in (0, 1, 2):
            out.append({'fn': 0x1D91A0, 'name': 'shell', 'capture': base, 'flag': flag, 'model': model,
                        'seed': 200 + 3 * flag + model})
    out.append({'fn': 0x1D91A0, 'name': 'soft colour', 'capture': base, 'flag': 1, 'model': 2,
                'soft': [0xFFFFFFFF, 0x80, 0x1FF, 0xFFFFFFFF]})
    out.append({'fn': 0x1D91A0, 'name': 'soft high', 'capture': base, 'flag': 0, 'model': 0,
                'soft': [0x80000000, 0x7F, 0x100, 0xFFFFFFFF80000000]})
    out.append({'fn': 0x1D91A0, 'name': 'soft sign16', 'capture': base, 'flag': 0, 'model': 1,
                'soft': [0x01, 0x8000, 0x00, 0x00]})
    out.append({'fn': 0x1D91A0, 'name': 'soft sign8', 'capture': base, 'flag': 1, 'model': 0,
                'soft': [0x01, 0x00, 0x800000, 0x00]})
    for chan in (0, 1, 2, 3):
        out.append({'fn': 0x1DA290, 'name': 'clear', 'capture': base, 'chan': chan, 'a1': chan * 7})
        out.append({'fn': 0x1DA1E0, 'name': 'direct', 'capture': base, 'chan': chan, 'a2': -chan})
        for fn in (0x1D4E20, 0x1D4EB0):
            out.append({'fn': fn, 'name': 'head', 'capture': base, 'chan': chan})
        for fn in (0x1D4B80, 0x1D4C30):
            out.append({'fn': fn, 'name': 'clip head', 'capture': base, 'chan': chan, 'model': chan % 3})
    return out


def case_eye(field, v):
    return v if field == 'eye' else (300.0, 230.0, 300.0, 1.0)


def main():
    global ELF, LIB
    start = time.monotonic()
    ELF = S.read_elf()
    LIB = build()
    ee = FallEE(ELF)
    actual = set()
    for fn, size in FUNCS.items():
        for pc in range(fn, fn + size, 4):
            w = ee.load(pc)
            if w >> 26 == 3:
                actual.add((w & 0x3FFFFFF) << 2)
    assert actual == set(SIG) - {0x187780}, (sorted(map(hex, actual - set(SIG))), sorted(map(hex, set(SIG) - actual)))
    all_cases = cases()
    selected = RM.select(all_cases, 60, 97112, axes=(lambda c: (c['fn'], c['name']),),
                         keep=lambda i, c: c['name'] in ('baseline', 'cone', 'shell', 'clear', 'direct', 'head',
                                                         'clip head'))
    RM.banner(RM.part(len(selected), len(all_cases), 'lamp cases'))
    for c in all_cases:
        if c['capture'] not in IMAGES:
            p = Path(c['capture'])
            IMAGES[c['capture']] = ((p / 'eeMemory.bin').read_bytes(), (p / 'scratchpad.bin').read_bytes())
    rows = RM.parallel_map(run_case, selected)
    fault_cases, firsts = [], set()
    for c, row in zip(selected, rows):
        if c['name'] == 'baseline' or (c['name'] in ('cone', 'shell', 'flare') and c['name'] not in firsts):
            firsts.add(c['name'])
            fault_cases.extend(dict(c, failat=k) for k in range(1, row[2] + 1))
    fault_rows = RM.parallel_map(run_case, fault_cases)
    # The mode / variant values 00188ED0 never passes: the translation refuses.
    e = prepare(dict(all_cases[0]))
    backing = (C.c_uint8 * len(e.mem)).from_buffer(e.mem)

    def mp(_, a, n_, w):
        return C.addressof(backing) + a if a + n_ < len(e.mem) else None
    for a1, a2 in ((2, 0), (0, 2), (-1, 1)):
        h = Host(None, MAP(mp), CALL(), STORE(), S.STACK_TOP, 0, 0, 0)
        assert LIB.em_aim_fire_lamp_00187780(C.byref(h), NODE, a1, a2) == -1 and h.fault == 4, (a1, a2)
    refusals = 3
    for fn in FUNCS:
        for absent in ('map', 'call', 'latched'):
            c = dict(all_cases[0], fn=fn)
            e = prepare(c)
            backing = (C.c_uint8 * len(e.mem)).from_buffer(e.mem)
            h = Host(None, MAP() if absent == 'map' else MAP(mp), CALL(), STORE(), S.STACK_TOP,
                     7 if absent == 'latched' else 0, 0, 0)
            args, fargs = args_of(c, e)
            rc = native(fn, C.byref(h), args, fargs, U32(0))
            if absent == 'call' and fn == 0x1DA1E0:                 # it calls nothing
                assert rc == 0 and not h.fault, (hex(fn), absent, rc, h.fault)
                continue
            assert rc == -1 and h.fault, (hex(fn), absent, rc, h.fault)
            refusals += 1
    branches = set().union(*(r[0] for r in rows))
    sites = set()
    for fn, size in FUNCS.items():
        for pc in range(fn, fn + size, 4):
            w = ee.load(pc)
            op, rs, rt = w >> 26, w >> 21 & 31, w >> 16 & 31
            if op in (4, 20) and rs == 0 and rt == 0:
                continue
            if ee.branch(w, pc) is not None:
                sites.add(pc)
    # 00187780's switches: a mode / variant other than 0 / 1 (never passed).
    unreachable = {(0x1877C4, False), (0x1877DC, False), (0x18787C, False)}
    gaps = {(pc, b) for pc in sites for b in (False, True)} - branches - unreachable
    if RM.FULL or os.environ.get('EM_AIM_FIRE_LAMP_GAPS'):
        assert not gaps, sorted((hex(pc), b) for pc, b in gaps)
    elif gaps:
        print('lamp: quick mode leaves branch outcomes to the full sweep:', len(gaps))
    print(f'PASS: {len(fault_rows)} callee failure cuts and {refusals} refusals')
    print(f'PASS: {len(selected)} cases, {sum(r[1] for r in rows)} ordered stores/calls, '
          f'{sum(r[2] for r in rows)} callee entries, {len(branches)} branch outcomes; '
          f'{time.monotonic() - start:.2f}s')


if __name__ == '__main__':
    main()
