#!/usr/bin/env python3
"""Original-instruction oracle for the aim camera (src/game/em_camera_aim.c,
docs/CAMERA_LIVE.md section 7).

The fourteen translated routines (camera actions 1 / 2 / 5 and what they
own, camera action 14 00198AF0 with its follow 00198930, and action 11's
raised target 00191530) execute from captured AREA11 RAM (the route snapshots 00..14 and the C10
aim beats' end snapshots): the original on one image, the native module over
a second copy through its map / call / store host. Compared: every direct
store in order (address, size, bytes, same-value stores included), every
callee entry (function, stack pointer, argument registers), the result, and
the whole RAM, scratchpad and stack at the end. The ELF supplies every
instruction; none are embedded here.

Callees: the SDK vector leaves, the chases, the math and the area clamp
execute their ORIGINAL instructions on both sides (nested, identically).
The rest are scripted contracts the case chooses (the solve 0018D7B0, the
release 00197490, the segment query 0019A910 with its 0x700031B0 / D0 / D4
words, the player moves, the render-context helpers, the sight drawers);
scripted callees are not claimed verified by this test. World cases run
EVERY callee as original code on both sides.

Default run ~10 s (mode quick); EM_TEST_FULL=1 runs every case.
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

OUT = ROOT / 'build/camera-aim'
DECOMP = ROOT.parent / 'Extermination'
ROUTE = DECOMP / 'build/s87/route'
AIM = DECOMP / 'build/aimfire/capture'

# The translated routines and their sizes (bytes of instructions).
FUNCS = {0x197D20: 0x324, 0x198650: 0x2D4, 0x18CA90: 0x140, 0x197740: 0x130, 0x197870: 0x4A4,
         0x198050: 0x1EC, 0x198440: 0x204, 0x198240: 0x200, 0x1912B0: 0xA0, 0x1999C0: 0x290,
         0x1DB800: 0x24, 0x198AF0: 0x1F0, 0x198930: 0x1B8, 0x191530: 0x50}
# Every other jal target: (integer argument registers, float argument registers).
SIG = {0x102948: (2, 0), 0x1029C0: (1, 0), 0x102C58: (3, 0), 0x1031E0: (2, 0), 0x1026A0: (3, 0),
       0x1028D0: (3, 0), 0x1028B8: (3, 0), 0x1028E8: (4, 0), 0x103230: (2, 1), 0x102738: (2, 0),
       0x102760: (2, 0), 0x102900: (2, 1), 0x11DF78: (0, 1), 0x11E748: (0, 1), 0x11E620: (0, 2),
       0x11E2A8: (0, 1), 0x11DE90: (0, 1), 0x1B1470: (0, 1), 0x18C4B0: (1, 2), 0x18C6A0: (2, 1),
       0x18C850: (1, 2), 0x18C920: (2, 1), 0x18D7B0: (2, 0), 0x191210: (0, 0), 0x197490: (3, 0),
       0x19A910: (3, 0), 0x183010: (2, 0), 0x182F90: (2, 0), 0x1D2610: (0, 1), 0x1D2830: (2, 0),
       0x21B9A0: (1, 2), 0x22E7F0: (1, 0), 0x1D2040: (2, 0), 0x1DB830: (2, 1), 0x1DBE20: (1, 1),
       0x199770: (1, 0), 0x1DB9D0: (2, 1), 0x1DC610: (1, 1), 0x1DC890: (1, 1), 0x1DBF00: (1, 1),
       0x1DC020: (1, 1), 0x1DC960: (1, 1), 0x1DBCB0: (1, 1), 0x1DBD50: (1, 1)}
# Original on both sides in every case (pure callees over memory).
REAL = {0x102948, 0x1029C0, 0x102C58, 0x1031E0, 0x1026A0, 0x1028D0, 0x1028B8, 0x1028E8, 0x103230,
        0x102738, 0x102760, 0x102900, 0x11DF78, 0x11E748, 0x11E620, 0x11E2A8, 0x11DE90, 0x1B1470,
        0x18C4B0, 0x18C6A0, 0x18C850, 0x18C920, 0x191210}
# Original too in a world case (the camera's own solve and release, the
# collision query and the player moves, the render-context helpers).
WORLD = {0x18D7B0, 0x197490, 0x19A910, 0x183010, 0x182F90, 0x1D2610, 0x1D2830, 0x21B9A0,
         0x1D2040}
CAM, PLAYER, EYE, TGT = 0x8101E0, 0x8102B0, 0x8105D0, 0x8105E0
NODE, WHO = 0x1E40000, 0x1E40400        # a scripted collision record and hit entity

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


ELF = LIB = None
IMAGES = {}
# entry -> (native symbol, argument names, has result)
ENTRIES = {0x197D20: ('00197D20', ('cam', 'pl'), False), 0x198650: ('00198650', ('cam', 'pl'), False),
           0x18CA90: ('0018CA90', ('cam', 'pl'), False), 0x197740: ('00197740', ('cam', 'pl'), False),
           0x197870: ('00197870', ('cam', 'pl', 'a2'), False),
           0x198050: ('00198050', ('cam', 'pl'), True), 0x198440: ('00198440', ('cam', 'pl', 'a2'), False),
           0x198240: ('00198240', ('pl', 'gun'), True), 0x1912B0: ('001912B0', ('pl',), False),
           0x1999C0: ('001999C0', ('pl', 'a1'), True), 0x1DB800: ('001DB800', (), False),
           0x198AF0: ('00198AF0', ('cam', 'pl'), False), 0x198930: ('00198930', ('cam', 'pl'), True),
           0x191530: ('00191530', ('cam', 'pl'), False)}


def build(source=None):
    OUT.mkdir(parents=True, exist_ok=True)
    source = source or os.environ.get('EM_CAMERA_AIM_SOURCE', str(ROOT / 'src/game/em_camera_aim.c'))
    lib = OUT / (Path(source).stem + ('.dylib' if sys.platform == 'darwin' else '.so'))
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', source, '-o', str(lib)], cwd=ROOT, check=True,
                   capture_output=True)
    dll = C.CDLL(str(lib))
    for entry, (name, args, result) in ENTRIES.items():
        types = [P(Host)] + [U32 if a in ('cam', 'pl', 'gun') else C.c_int32 for a in args]
        if result:
            types.append(P(C.c_int32))
        getattr(dll, 'em_cam_aim_' + name).argtypes = types
    return dll


def in_funcs(pc):
    return any(start <= pc < start + size for start, size in FUNCS.items())


class Oracle(FallEE):
    def __init__(self, ram, spad):
        super().__init__(ELF, ram, spad)
        self.events = []
        self.active = False
        self.in_hook = False
        self.branches = set()

    def save(self, a, v, size=4):
        super().save(a, v, size)
        if self.active and not self.in_hook and (a & 0xFFFFFFFF) < 0x7F000000:
            self.events.append(('store', a & 0xFFFFFFFF, size, self.read(a, size)))

    def branch(self, w, pc):
        b = super().branch(w, pc)
        if b is not None and in_funcs(pc):
            self.branches.add((pc, b[0]))
        return b


def nested(e, fn):
    saved = (list(e.r), list(e.rh), list(e.f), e.hi, e.lo, e.acc, e.cond, [list(v) for v in e.vf],
             list(e.vacc), e.q)
    hooks, e.hooks = e.hooks, {}
    e.r[29] = (e.r[29] - 0x400) & ~15
    e.r[31] = S.RETURN
    e.run(fn)
    v, f = e.r[2], e.f[0]
    e.r, e.rh, e.f, e.hi, e.lo, e.acc, e.cond, e.vf, e.vacc, e.q = saved
    e.hooks = hooks
    e.r[2] = v
    e.f[0] = f


def policy(e, fn, case, state):
    """What a callee does. REAL ones (and WORLD ones in a world case) run
    their original instructions; the others follow the case's script."""
    a = [int(e.r[4 + i]) & 0xFFFFFFFF for i in range(4)]
    e.r[2] = 0
    e.f[0] = 0
    if fn in REAL or (case.get('world') and fn in WORLD):
        nested(e, fn)
        return
    state['n'] = state.get('n', 0) + 1
    if fn == 0x18D7B0:
        e.r[2] = case.get('solve', 0)
        if case.get('solve_writes'):                 # the solve moves the desired eye
            e.save(CAM + 0x14, S.bits(case['solve_writes']))
    elif fn == 0x197490:
        if case.get('release_writes'):
            e.save(CAM + 1, 0, 1)
    elif fn == 0x19A910:
        hit = case.get('hit', 0)
        e.r[2] = hit
        if hit:
            for i, v in enumerate(case.get('point', (380.0, 190.0, 300.0, 1.0))):
                e.save(0x700031B0 + 4 * i, S.bits(v))
            e.save(0x700031D0, NODE if hit == 2 or case.get('node_on_1') else 0)
            e.save(0x700031D4, WHO if case.get('who') else 0)
    elif fn == 0x1999C0 or fn == 0x1DBCB0:
        pass
    # every other scripted callee records its arguments only


def prepare(case):
    ram, spad = IMAGES[case['image']]
    e = Oracle(ram, spad)
    for address, size, value in case['poke']:
        e.save(address, value, size)
    # the scripted collision record and hit entity
    e.write(NODE, bytes(0x40))
    e.save(NODE + 0x1A, case.get('attr', 0x2000), 2)
    for i, v in enumerate(case.get('normal', (0.0, 1.0, 0.0))):
        e.save(NODE + 0x24 + 4 * i, S.bits(v))
    e.write(WHO, bytes(0x20))
    e.save(WHO + 3, case.get('who_type', 0x54), 1)
    return e


def args_of(case):
    out = []
    for name in ENTRIES[case['fn']][1]:
        out.append({'cam': CAM, 'pl': PLAYER, 'gun': case.get('gun_arg', 0),
                    'a2': case.get('a2', 0), 'a1': case.get('a1', 0)}[name])
    return out


def run_case(case):
    e = prepare(case)
    n = Oracle(e.mem, e.spad)
    n.stack[:] = e.stack
    ostate, nstate = {}, {}

    def hook(fn):
        def inner(x):
            na, nf = SIG[fn]
            x.events.append(('call', fn, x.r[29] & 0xFFFFFFFF,
                             tuple(v & 0xFFFFFFFF for v in x.r[4:4 + na]),
                             tuple(v & 0xFFFFFFFF for v in x.f[12:12 + nf])))
            x.in_hook = True
            policy(x, fn, case, ostate)
            x.in_hook = False
        return inner
    e.hooks = {fn: hook(fn) for fn in SIG}
    e.active = True
    args = args_of(case)
    e.call(case['fn'], args)
    native_events, errors, calls = [], [], [0]
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
        native_events.append(('store', a, size, n.read(a, size)))

    def worker(_, p):
        c = p.contents
        try:
            fn = c.function
            na, nf = SIG[fn]
            assert (c.na, c.nf) == (na, nf), (hex(fn), 'signature', c.na, c.nf)
            native_events.append(('call', fn, c.sp, tuple(int(v) & 0xFFFFFFFF for v in c.a[:na]),
                                   tuple(int(v) for v in c.f[:nf])))
            k = len(native_events)
            assert native_events == e.events[:k], (case['name'], 'events before callee', k,
                                                   first_diff(native_events, e.events))
            calls[0] += 1
            if calls[0] == case.get('failat'):
                return -1
            n.r[29] = c.sp
            for i, v in enumerate(c.a[:na]):
                n.r[4 + i] = S.sx32(int(v) & 0xFFFFFFFF)
            for i, v in enumerate(c.f[:nf]):
                n.f[12 + i] = v
            policy(n, fn, case, nstate)
            c.v0 = n.r[2] & 0xFFFFFFFFFFFFFFFF
            c.f0 = n.f[0] & 0xFFFFFFFF
            return 0
        except Exception as ex:
            errors.append(ex)
            return -1
    callbacks = (MAP(mapping), CALL(worker), STORE(observe))
    h = Host(None, *callbacks, S.STACK_TOP, 0, 0, 0)
    name, _, has_result = ENTRIES[case['fn']]
    out = C.c_int32(0x5EED)
    call_args = [C.byref(h)] + args + ([C.byref(out)] if has_result else [])
    status = getattr(LIB, 'em_cam_aim_' + name)(*call_args)
    if errors:
        raise errors[0]
    if 'failat' in case:
        assert status == -1 and h.fault == 3 and h.fault_function == case['fn'], (case['name'], 'failure latch',
                                                                                status, h.fault)
        assert native_events == e.events[:len(native_events)], (case['name'], 'failure prefix')
        assert not has_result or out.value == 0x5EED, (case['name'], 'failure result')
        before = list(native_events)
        assert getattr(LIB, 'em_cam_aim_' + name)(*call_args) == -1
        assert native_events == before, (case['name'], 'latched call had effects')
        return set(), len(native_events), calls[0]
    assert status == 0 and h.fault == 0, (case['name'], status, h.fault, hex(h.fault_function),
                                          hex(h.fault_address))
    assert native_events == e.events, (case['name'], 'events', first_diff(native_events, e.events),
                                       len(native_events), len(e.events))
    if has_result:
        assert out.value == S.s32(e.r[2] & 0xFFFFFFFF), (case['name'], 'result', out.value, e.r[2])
    if n.mem != e.mem or n.spad != e.spad:
        where = [hex(i) for i in range(len(e.mem)) if n.mem[i] != e.mem[i]][:8]
        where += [hex(0x70000000 + i) for i in range(len(e.spad)) if n.spad[i] != e.spad[i]][:8]
        raise AssertionError((case['name'], 'whole memory differs', where))
    return e.branches, len(e.events), sum(x[0] == 'call' for x in e.events)


def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i, x, y
    return min(len(a), len(b)), a[len(b):len(b) + 1], b[len(a):len(a) + 1]


# ======================================================================
# Cases
# ======================================================================

def F(v):
    return S.bits(v)


def images():
    out = []
    for folder in (ROUTE, AIM):
        if not folder.exists():
            continue
        for beat in sorted(p for p in folder.iterdir() if p.is_dir()):
            if folder == ROUTE and not RM.in_scope_beat(beat.name):
                continue
            if (beat / 'eeMemory.bin').exists() and (beat / 'scratchpad.bin').exists():
                out.append(str(beat))
    return out


def random_case(rng, fn, image, k):
    """One case: the entry, its image and the pokes that steer it."""
    poke = []
    choose = rng.choice

    def b(address, value):
        poke.append((address, 1, value))

    def h16(address, value):
        poke.append((address, 2, value & 0xFFFF))

    def w(address, value):
        poke.append((address, 4, value if isinstance(value, int) else F(value)))
    case = {'fn': fn, 'image': image, 'name': f'{fn:06X}/{Path(image).name}/{k}'}
    # the camera block
    b(CAM + 1, choose((0, 1, 1, 2, 2, 2, 3, 4, 5)))
    b(CAM + 5, 0)
    b(CAM + 0x8B, choose((0, 0, 1)))
    h16(CAM + 8, choose((0, 1, 13, 14, 15, 30, 0xFFFF)))
    h16(CAM + 0x5A, choose((0, 0x10, 0x8810, 0x80)))
    w(CAM + 0x68, choose((0.0, 0.5, 0.99, 1.0, 0.01, -0.0)))
    w(CAM + 0x54, choose((rng.uniform(150.0, 260.0), 1000.0, 0.0)))
    w(CAM + 0xC, choose((rng.uniform(-40.0, 40.0), -20.0)))
    # the player
    code = choose((0xD, 0xD, 0x2A, 0xC, 0xC, 0x29, 1, 0x30, 6))
    w(PLAYER + 0x230, code)
    b(PLAYER + 0x1F1, choose((0, 1, 1, 2)))
    if rng.random() < 0.3:                       # the eye low and close (00197D20's swing-out)
        case['eye_close'] = 1
    # the gun's barrel pitch (the -22 / -25 limits and the clamps)
    if rng.random() < 0.6:
        y = choose((0.95, 0.9, 0.8, 0.76, 0.5, 0.3, 0.1, -0.2, -0.45, -0.9))
        hz = max(0.0, 1.0 - y * y) ** 0.5
        case['gun_dir'] = (hz * 0.7, y, hz * 0.7)
    if rng.random() < 0.35:                      # a steep published pitch (the near-wall lift)
        w(0x70003B50, choose((1.40, -1.40, 1.50, 1.2)))
    if rng.random() < 0.4:                       # the gun's hip close to its muzzle (00198240 < 9)
        case['hip_near'] = 1
    # globals
    b(0x810CA4, choose((0xFF, 0xFF, 0xFF, 0, 1, 2)))
    b(0x810CA5, choose((5, 5, 6)))
    b(0x810CA7, choose((7, 7, 8, 9)))
    b(0x8106C6, choose((0, 0, 1)))
    h16(0x810E70, choose((0, 4, 1, 5, 0x10)))
    b(0x810700, choose((0xB, 0xB, 0x10)))
    b(0x810702, choose((0, 0, 3)))
    b(0x81078B, choose((0, 0xFF)))
    if rng.random() < 0.2:
        case['dot'] = 1
    case['a1'] = choose((0, 1))
    case['a2'] = choose((0, 1, 2))
    case['hit'] = choose((0, 0, 1, 2))
    case['who'] = choose((0, 1))
    case['who_type'] = choose((0x54, 0x55))
    case['attr'] = choose((0x2000, 0x0, 0x2050))
    case['normal'] = choose(((0.0, 1.0, 0.0), (0.6, 0.0, -0.8), (-1.0, 0.0, 0.0)))
    case['node_on_1'] = choose((0, 1))
    case['solve'] = choose((0, 1, 2))
    case['solve_writes'] = choose((None, None, 250.0))
    case['release_writes'] = choose((0, 1))
    case['poke'] = poke
    return case


def finish(case):
    """Pokes that depend on the image (the player's position, the gun's)."""
    ram, spad = IMAGES[case['image']]
    e = FallEE(ELF, ram, spad)
    gun = e.load(PLAYER + 0x20)
    if case['fn'] == 0x198240:
        case['gun_arg'] = gun
    if 'dot' in case:
        case['poke'].append((gun + 0x210, 4, case['dot']))
        for i, v in enumerate((385.0, 200.0, 290.0, 1.0)):
            case['poke'].append((gun + 0x200 + 4 * i, 4, F(v)))
    if 'gun_dir' in case:
        for i, v in enumerate(case['gun_dir']):
            case['poke'].append((gun + 0xC0 + 4 * i, 4, F(v)))
    if 'hip_near' in case:
        for i in range(3):
            case['poke'].append((gun + 0xB0 + 4 * i, 4, (e.load(gun + 0xA0 + 4 * i) + (0x100 if i != 1 else 0))
                                 & 0xFFFFFFFF))
    if case.get('eye_close'):
        px, py, pz = (S.flt(e.load(PLAYER + 0xA0 + 4 * i)) for i in range(3))
        for i, v in enumerate((px + 3.0, py + 10.0, pz + 2.0)):
            case['poke'].append((EYE + 4 * i, 4, F(v)))
            case['poke'].append((CAM + 0x10 + 4 * i, 4, F(v)))
    return case


def cases():
    imgs = images()
    out = []
    rng = random.Random(0xA1C4)
    weights = ((0x197D20, 9), (0x198650, 9), (0x18CA90, 2), (0x197740, 2), (0x197870, 7), (0x198050, 4),
               (0x198440, 6), (0x198240, 6), (0x1912B0, 3), (0x1999C0, 6), (0x1DB800, 1))
    for k in range(RM.pick(1800, 1800)):
        fn = rng.choices([f for f, _ in weights], [w for _, w in weights])[0]
        out.append(random_case(rng, fn, rng.choice(imgs), k))
    # edge cases: the zoom ramp (D_00810CA7 = 8 makes 001999C0 return 1), the
    # area-0x10 lift at and past z 473, the laser dot's +0x210 / D_008106C6
    grid_image = next((i for i in imgs if 'aim_00' in i), imgs[0])
    for state in range(6):
        for code in (0xD, 0x2A, 0xC, 0x29, 1):
            for ready in (0, 1):
                poke = [(CAM + 1, 1, state), (CAM + 5, 1, 0), (PLAYER + 0x230, 4, code), (PLAYER + 0x1F1, 1, ready),
                        (CAM + 0x8B, 1, ready)]
                for fn in (0x197D20, 0x198650):
                    out.append({'fn': fn, 'image': grid_image, 'poke': list(poke),
                                'name': f'grid {fn:06X} {state} {code:#x} {ready}'})
    for state in (3, 4):                        # the night-vision release arms (D_00810CA7 = 9)
        for fn in (0x197D20, 0x198650):
            poke = [(CAM + 1, 1, state), (CAM + 5, 1, 0), (PLAYER + 0x230, 4, 0xD), (0x810CA7, 1, 9)]
            out.append({'fn': fn, 'image': grid_image, 'poke': poke, 'name': f'grid {fn:06X} {state} night'})
    for mode in (0, 1, 2, 0xFF):
        for kind in (7, 8, 9):
            for marker in (5, 6):
                for a1 in (0, 1):
                    poke = [(0x810CA4, 1, mode), (0x810CA7, 1, kind), (0x810CA5, 1, marker)]
                    out.append({'fn': 0x1999C0, 'image': grid_image, 'poke': poke, 'a1': a1,
                                'name': f'grid sight {mode} {kind} {marker} {a1}'})
    for image in imgs[:3]:
        for held in (4, 1, 0, 5):
            for zoom in (0.99, 1.0, 0.5, 0.01, 0.0):
                poke = [(CAM + 1, 1, 2), (CAM + 5, 1, 0), (PLAYER + 0x230, 4, 0xC), (0x810CA4, 1, 0xFF),
                        (0x810CA7, 1, 8), (0x810E70, 2, held), (CAM + 0x68, 4, F(zoom))]
                out.append({'fn': 0x198650, 'image': image, 'poke': poke, 'name': f'ramp {held} {zoom}'})
        for z in (400.0, 472.99997, 473.0, 500.0):
            poke = [(0x810700, 1, 0x10), (0x810702, 1, 0), (0x81078B, 1, 0), (PLAYER + 0xA8, 4, F(z))]
            out.append({'fn': 0x1912B0, 'image': image, 'poke': poke, 'name': f'lift {z}'})
        for code in (0xD, 0x2A):
            for state in (2, 4):
                poke = [(CAM + 1, 1, state), (PLAYER + 0x230, 4, code)]
                out.append({'fn': 0x197D20, 'image': image, 'poke': poke, 'name': f'hold {code:#x} {state}'})
        for pitch in (1.4, -1.4):
            for gy in (0.0, -0.3, 0.5, 0.9, 0.8333333, 0.8333334, 0.73333335, 0.7333333):
                for top in (0.0, 1000.0):
                    hz = (1.0 - gy * gy) ** 0.5 * 0.7
                    poke = [(0x70003B50, 4, F(pitch)), (CAM + 0x54, 4, F(top)), (PLAYER + 0x230, 4, 0xD)]
                    out.append({'fn': 0x197870, 'image': image, 'poke': poke, 'gun_dir': (hz, gy, hz),
                                'a2': 0, 'name': f'near {pitch} {gy} {top}'})
        for dot in (0, 1):
            for c6 in (0, 1):
                for a2 in (0, 1, 2):
                    poke = [(0x8106C6, 1, c6)]
                    out.append({'fn': 0x198440, 'image': image, 'poke': poke, 'a2': a2, 'dot': dot,
                                'hit': 1, 'name': f'dot {dot} {c6} {a2}'})
    # camera action 14 (00198AF0, player code 0x28) and its follow 00198930,
    # action 11's raised target 00191530: every sub-state, the code 0x28 or
    # not, +1F1 0 / 1 / 2, camera mode +5 0 / 1 (the whole grid on one image,
    # kept in quick mode; on every other image a sample)
    for image in imgs:
        tag = 'grid' if image == grid_image else 'a14'
        for state in range(5):
            for code in (0x28, 0x12):
                for sub in (0, 1, 2):
                    for mode in (0, 1):
                        poke = [(CAM + 1, 1, state), (CAM + 5, 1, mode), (PLAYER + 0x230, 4, code),
                                (PLAYER + 0x1F1, 1, sub)]
                        out.append({'fn': 0x198AF0, 'image': image, 'poke': poke,
                                    'name': f'{tag} 198AF0 {state} {code:#x} {sub} {mode} {Path(image).name}'})
        for fn in (0x198930, 0x191530):
            out.append({'fn': fn, 'image': image, 'poke': [], 'name': f'{tag} {fn:06X} {Path(image).name}'})
    # world cases: every callee original, on every image, the roots in their
    # AREA11 states (D_00810CA4..CA7 as captured)
    for image in imgs:
        for fn, state, code in ((0x197D20, 0, 0xD), (0x197D20, 1, 0xD), (0x197D20, 2, 0xD), (0x197D20, 2, 0xC),
                                (0x197D20, 4, 0xD), (0x197D20, 3, 0xD), (0x198650, 0, 0xC), (0x198650, 1, 0xC),
                                (0x198650, 2, 0xC), (0x198650, 2, 0xD), (0x198650, 2, 1), (0x198650, 3, 0xC),
                                (0x18CA90, 0, 1), (0x198AF0, 0, 0x28), (0x198AF0, 1, 0x28),
                                (0x198AF0, 3, 0x28)):
            poke = [(CAM + 1, 1, state), (CAM + 5, 1, 0), (PLAYER + 0x230, 4, code), (PLAYER + 0x1F1, 1, 1)]
            out.append({'fn': fn, 'image': image, 'world': 1, 'poke': poke,
                        'name': f'world {fn:06X} {state} {code:#x} {Path(image).name}'})
    return out


def main():
    global ELF, LIB
    start = time.monotonic()
    ELF = S.read_elf()
    LIB = build()
    ee = FallEE(ELF)
    targets = set()
    for fn, size in FUNCS.items():
        for pc in range(fn, fn + size, 4):
            word = ee.load(pc)
            if word >> 26 == 3:
                targets.add((word & 0x3FFFFFF) << 2)
    assert targets - set(FUNCS) == set(SIG), (sorted(hex(t) for t in targets - set(FUNCS) - set(SIG)),
                                              sorted(hex(t) for t in set(SIG) - targets))
    imgs = images()
    if not imgs:
        raise SystemExit('no captured AREA11 images under %s or %s' % (ROUTE, AIM))
    for path in imgs:
        p = Path(path)
        IMAGES[path] = ((p / 'eeMemory.bin').read_bytes(), (p / 'scratchpad.bin').read_bytes())
        ram = IMAGES[path][0]
        for fn, size in list(FUNCS.items()) + [(f, 4) for f in SIG]:
            at = fn
            assert ram[at:at + size] == bytes(ee.mem[at:at + size]), (path, 'code differs from the ELF', hex(fn))
    all_cases = [finish(c) for c in cases()]
    selected = RM.select(all_cases, 800, 0xA1C4, axes=(lambda c: c['fn'], lambda c: c.get('world', 0)),
                         keep=lambda i, c: (bool(c.get('world')) and 'aim_00' in c['image']) or
                                          c['name'].split()[0] in ('ramp', 'lift', 'dot', 'hold', 'near', 'grid'))
    RM.banner(RM.part(len(selected), len(all_cases), 'aim camera cases'))
    rows = RM.parallel_map(run_case, selected)
    branches = set().union(*(r[0] for r in rows))
    sites = set()
    for fn, size in FUNCS.items():
        for pc in range(fn, fn + size, 4):
            word = ee.load(pc)
            op, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
            if op in (4, 20) and rs == 0 and rt == 0:
                continue
            if ee.branch(word, pc) is not None:
                sites.add(pc)
    if True:
        gaps = sorted((hex(pc), b) for pc in sites for b in (False, True) if (pc, b) not in branches)
        assert not gaps, ('branch outcomes never exercised', gaps)
    covered = sum(1 for pc in sites for b in (False, True) if (pc, b) in branches)
    # fault cuts: every callee of a few cases fails in turn
    fault_cases = []
    for c, row in zip(selected, rows):
        if c.get('world') and 'aim_00' in c['image'] and c['fn'] in (0x197D20, 0x198650, 0x198AF0):
            fault_cases.extend(dict(c, failat=k) for k in range(1, row[2] + 1))
    fault_rows = RM.parallel_map(run_case, fault_cases)
    refusals = 0
    for fn, (name, args, has_result) in ENTRIES.items():
        for absent in ('map', 'call', 'latched'):
            # 00198AF0 reaches its callees only with the player code 0x28
            poke = [(CAM + 1, 1, 0)] + ([(PLAYER + 0x230, 4, 0x28)] if fn == 0x198AF0 else [])
            case = finish({'fn': fn, 'image': imgs[0], 'poke': poke, 'name': 'refusal'})
            e = prepare(case)
            backing = (C.c_uint8 * len(e.mem)).from_buffer(e.mem)

            def mp(_, a, size, write):
                return C.addressof(backing) + a if a + size < len(e.mem) else None
            h = Host(None, MAP() if absent == 'map' else MAP(mp), CALL(), STORE(), S.STACK_TOP,
                     7 if absent == 'latched' else 0, 0, 0)
            out = C.c_int32(0x5EED)
            call_args = [C.byref(h)] + args_of(case) + ([C.byref(out)] if has_result else [])
            rc = getattr(LIB, 'em_cam_aim_' + name)(*call_args)
            if absent == 'call' and fn in (0x1DB800, 0x1912B0, 0x1999C0):
                # these reach no callee from this state (area 0xB; D_00810CA4 = 0xFF,
                # D_00810CA7 = 7, a1 = 0: 001DB800 only, which is translated here)
                assert rc == 0 and h.fault == 0, (hex(fn), absent, rc, h.fault)
                continue
            assert rc == -1 and h.fault and out.value == 0x5EED, (hex(fn), absent, rc, h.fault)
            refusals += 1
    print('camera aim vs original instructions: PASS %d cases (%d world), %d ordered stores / callee '
          'entries, %d callee entries; %d of %d branch outcomes%s; %d callee failure cuts; %d host refusals '
          '(%.1fs)' % (len(selected), sum(1 for c in selected if c.get('world')), sum(r[1] for r in rows),
                       sum(r[2] for r in rows), covered, 2 * len(sites),
                       ' (all, asserted)',
                       len(fault_rows), refusals, time.monotonic() - start))


if __name__ == '__main__':
    main()
