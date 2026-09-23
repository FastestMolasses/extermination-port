#!/usr/bin/env python3
"""Execute the original +4 = 2 player states and compare em_player_major2.c.

docs/PLAYER_MAJOR2.md. The captured AREA11 RAM image (state04,
../Extermination/build/startup-reference/playable_ee.bin) supplies every
instruction; its code bytes for each routine run here are checked against the
user's pinned ELF first. Nothing original is embedded in this file.

Executed, unmodified:
  0021E830 00221FC0 00222580 00222AD0 002230A0 00225570 002255C0   (state2[3..7, 0x16, 0x19])
  00181110 00181180 00181D70 001823E0                               (the entry routines)
  0015B770                       (the dispatcher, with the native routines bound into
                                  em_player_stage_0015B770's state2[] table)

Every callee is hooked and recorded (never simulated): each hook logs its
arguments, returns a scripted value and applies the case's scripted writes to
the record (and to the skeleton node), identically on both sides, so the
order of every read against every call is checked. The comparison covers all
0x320 record bytes, D_00810707, D_00275B14, D_008106F1, the return values and
the callee sequence with its arguments.

Float arithmetic: the shared EE interpreter's COP1 is known to differ from
the measured EE (docs/EE_FLOAT_MODEL.md section 5a), so this oracle routes
every COP1 arithmetic op and compare through tools/ee_float_model.py; a COP1
op outside the model, or any VU0 macro op, stops the test.

Default run: every path class plus a fixed-seed random sample; EM_TEST_FULL=1
runs the exhaustive sweep.
"""
import ctypes as C
import hashlib
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import ee_float_model as efm  # noqa: E402
import reference_mode  # noqa: E402
from test_player_slide_reference import EE, read_elf, sx32, s32  # noqa: E402
from test_player_floor_reference import (LiveActor, StageScene, StageWorkers, Stage,  # noqa: E402
                                         STATE_FN, RESULT_FN)

DECOMP = ROOT.parent / 'Extermination'
REFERENCE = DECOMP / 'build/startup-reference'
WORLD_RAM = REFERENCE / 'playable_ee.bin'
ROUTE = DECOMP / 'build/s87/route'
SEED_CAPTURES = (ROUTE / '05_boxes/eeMemory.bin', ROUTE / '10_cage_roof_roger/eeMemory.bin')
PLAYER = 0x8102B0                 # the player record (D_008104C4 is its +214)
PTRS, NODE = 0x1F80000, 0x1F80100  # scratch: [D_00275B40] -> PTRS, [PTRS] -> NODE
D_8106F1, D_810707, D_275B14, D_275B40 = 0x8106F1, 0x810707, 0x275B14, 0x275B40

STATES = {'0021E830': 0x21E830, '00221FC0': 0x221FC0, '00222580': 0x222580,
          '00222AD0': 0x222AD0, '002230A0': 0x2230A0, '00225570': 0x225570,
          '002255C0': 0x2255C0}
SLOT = {'0021E830': 3, '00221FC0': 4, '00222580': 5, '00222AD0': 6, '002230A0': 7,
        '00225570': 0x16, '002255C0': 0x19}
ENTRIES = {'00181110': 0x181110, '00181180': 0x181180, '00181D70': 0x181D70,
           '001823E0': 0x1823E0}
DISPATCH, REACTION = 0x15B770, 0x21C440
# Sizes of the routines executed here (their code is compared to the ELF).
SIZES = {0x21E830: 388, 0x221FC0: 1460, 0x222580: 1348, 0x222AD0: 1484, 0x2230A0: 1616,
         0x225570: 76, 0x2255C0: 280, 0x181110: 108, 0x181180: 108, 0x181D70: 168,
         0x1823E0: 68, 0x15B770: 728}
# 0015B770's other targets (not translated here): hooked on both sides.
OTHER_STATE2 = {0: 0x21D800, 23: 0x21D800, 1: 0x21E240, 2: 0x21E490, 24: 0x21E490,
                9: 0x2236F0, 10: 0x223C70, 11: 0x21F330, 12: 0x21F850, 15: 0x2202C0,
                16: 0x21DBB0, 17: 0x21E9C0, 18: 0x21EAD0, 19: 0x21EAD0, 20: 0x21EF30,
                21: 0x224FE0}
PHASE13 = (0x21FB40, 0x2208C0, 0x220D30, 0x221630, 0x221C70)
PHASE14 = (0x21FED0, 0x220B50, 0x221060, 0x2217C0)

# Callees: address -> (log name, argument kinds). Kinds: 'p' the record
# pointer, 'i' an integer register, 'f' $f12 bits, 'q' $a1 relative to p.
CALLEES = {
    0x1749A0: ('request', 'piif'), 0x1FBD50: ('sound', 'piif'), 0x1B61C0: ('cue', 'iiii'),
    0x1EFE00: ('001EFE00', 'ip'), 0x15C1F0: ('0015C1F0', 'p'), 0x21E650: ('0021E650', 'p'),
    0x21D2E0: ('0021D2E0', 'pii'), 0x179880: ('drop', 'pq'), 0x175900: ('floor', 'pi'),
    0x178B90: ('translate', 'pi'), 0x182870: ('land_sound', 'pi'), 0x21D250: ('0021D250', 'pi'),
    0x21D490: ('0021D490', 'p'), 0x21C120: ('0021C120', 'p'), 0x21C190: ('0021C190', 'p'),
    0x21C200: ('0021C200', 'p'), 0x21C270: ('0021C270', 'p'), 0x21C350: ('0021C350', 'p'),
    0x17FC80: ('0017FC80', 'pf'), 0x17FF80: ('0017FF80', 'pf'), 0x122BB8: ('random', ''),
    0x188550: ('00188550', 'p'), 0x1885B0: ('001885B0', 'p'),
}
RESULTS = ('floor', '0021C190', 'random', '00188550', '001885B0')


# --------------------------------------------------------------- oracle core

class ModelEE(EE):
    """The shared EE interpreter with COP1 arithmetic and compares routed
    through ee_float_model (the measured EE) and VU0 macro ops refused."""

    def cop1(self, word, pc):
        rs, ft, fs, fd, fn = word >> 21 & 31, word >> 16 & 31, word >> 11 & 31, word >> 6 & 31, word & 63
        f = self.f
        if rs == 16:
            binary = {0: efm.ee_add, 1: efm.ee_sub, 2: efm.ee_mul, 3: efm.ee_div}
            if fn in binary: f[fd] = binary[fn](f[fs], f[ft]); return
            if fn == 6: f[fd] = efm.ee_mov(f[fs]); return
            if fn == 7: f[fd] = efm.ee_neg(f[fs]); return
            if fn == 36: f[fd] = efm.ee_cvt_w_s(f[fs]); return
            if fn == 48: self.cond = False; return
            if fn == 50: self.cond = bool(efm.ee_c_eq(f[fs], f[ft])); return
            if fn == 52: self.cond = bool(efm.ee_c_lt(f[fs], f[ft])); return
            if fn == 54: self.cond = bool(efm.ee_c_le(f[fs], f[ft])); return
            raise AssertionError(('COP1 op outside this oracle', fn, hex(pc)))
        if rs == 20:
            if fn == 32: f[fd] = efm.ee_cvt_s_w(f[fs]); return
            raise AssertionError(('COP1 W op outside this oracle', fn, hex(pc)))
        super().cop1(word, pc)          # mfc1 / mtc1 / cfc1 / ctc1

    def macro(self, word):
        raise AssertionError(('VU0 macro op reached in this oracle', hex(word)))


class Script:
    """One case's scripted callee behaviour, shared by both sides."""

    def __init__(self, results, writes, node_writes):
        self.results = results          # name -> list of return values (cycled)
        self.writes = writes            # name -> [(offset, byte)] applied on each call
        self.node_writes = node_writes  # name -> [(offset, word)] into the node
        self.count = {}

    def result(self, name):
        values = self.results.get(name, [0])
        n = self.count.get(name, 0)
        self.count[name] = n + 1
        return values[n % len(values)]


class Oracle:
    def __init__(self, ram):
        self.ee = ModelEE(None, ram=ram)
        self.ee.limit = 200_000
        for address, (name, kinds) in CALLEES.items():
            self.ee.hooks[address] = self.hook(name, kinds)
        for address in set(OTHER_STATE2.values()) | set(PHASE13) | set(PHASE14):
            self.ee.hooks[address] = self.hook(('state', address), 'p')
        self.ee.hooks[REACTION] = self.hook('reaction', 'p')

    def hook(self, name, kinds):
        def run(ee):
            args = []
            for i, kind in enumerate(kinds):
                value = ee.r[4 + i] & 0xFFFFFFFF
                if kind == 'p':
                    args.append('p' if value == PLAYER else ('bad', hex(value)))
                elif kind == 'q':
                    args.append(value - PLAYER)
                elif kind == 'f':
                    args.append(('f', ee.f[12] & 0xFFFFFFFF))
                else:
                    args.append(s32(value))
            ee.log.append((name,) + tuple(args))
            script = self.script
            for offset, value in script.writes.get(name, ()):
                ee.save(PLAYER + offset, value, 1)
            for offset, value in script.node_writes.get(name, ()):
                ee.save(NODE + offset, value)
            ee.r[2] = sx32(script.result(name) if name in RESULTS else 0)
        return run

    def run(self, raw, scene, node, script, entry, args=()):
        ee = self.ee
        ee.write(PLAYER, bytes(raw))
        ee.save(D_8106F1, scene['d8106F1'], 1)
        ee.save(D_810707, scene['d810707'], 1)
        ee.save(D_275B14, scene['d275B14'] & 0xFFFFFFFF)
        ee.save(D_275B40, PTRS)
        ee.save(PTRS, NODE)
        ee.save(NODE + 4, node[4])
        ee.save(NODE + 8, node[8])
        ee.log = []
        self.script = script
        ee.r = [0] * 32
        ee.rh = [0] * 32
        ee.r[28], ee.r[29] = 0x27D370, 0x7F0F0000
        ee.call(entry, (PLAYER,) + tuple(args))
        return {
            'bytes': ee.read(PLAYER, 0x320), 'log': ee.log, 'ret': s32(ee.r[2]),
            'd8106F1': ee.load(D_8106F1, 1), 'd810707': ee.load(D_810707, 1),
            'd275B14': s32(ee.load(D_275B14)),
            'node': {4: ee.load(NODE + 4), 8: ee.load(NODE + 8)},
        }


# --------------------------------------------------------------- native side

class Scene(C.Structure):
    _fields_ = [('d8106F1', C.c_uint8), ('d810707', C.c_uint8), ('d275B14', C.c_int32)]


P = C.POINTER(LiveActor)
REQUEST_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, P, C.c_int, C.c_int, C.c_float)
CUE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_int)
IDP_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, P)
ACT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, P)
ACT2_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, P, C.c_int, C.c_int)
ACTI_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, P, C.c_int)
ACTIR_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, P, C.c_int, C.POINTER(C.c_int))
ACTR_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, P, C.POINTER(C.c_int))
ACTF_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, P, C.c_float)
RAND_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_uint32))
NODE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint, C.POINTER(C.c_uint32))

WORKER_FIELDS = [
    ('request', REQUEST_FN), ('sound', REQUEST_FN), ('cue', CUE_FN), ('w001EFE00', IDP_FN),
    ('w0015C1F0', ACT_FN), ('w0021E650', ACT_FN), ('w0021D2E0', ACT2_FN), ('drop', ACT_FN),
    ('floor', ACTIR_FN), ('translate', ACTI_FN), ('land_sound', ACTI_FN), ('w0021D250', ACTI_FN),
    ('w0021D490', ACT_FN), ('w0021C120', ACT_FN), ('w0021C200', ACT_FN), ('w0021C270', ACT_FN),
    ('w0021C350', ACT_FN), ('w0021C190', ACTR_FN), ('w0017FC80', ACTF_FN), ('w0017FF80', ACTF_FN),
    ('random', RAND_FN), ('w00188550', ACTR_FN), ('w001885B0', ACTR_FN), ('root_node', NODE_FN),
]


class Workers(C.Structure):
    _fields_ = [('context', C.c_void_p)] + WORKER_FIELDS


class Major2(C.Structure):
    _fields_ = [('workers', C.POINTER(Workers)), ('scene', C.POINTER(Scene))]


def fbits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


class Native:
    """The same callees for em_player_major2.c, logging in the oracle's form."""

    def __init__(self, lib):
        self.lib = lib
        self.log = []
        self.keep = []
        self.live = None
        self.node = {4: 0, 8: 0}
        self.script = None
        self.fail = None        # a worker name that returns -1 (fault test)
        w = self.workers = Workers()

        def p(actor):
            return 'p' if C.addressof(actor.contents) == C.addressof(self.live) else ('bad', 0)

        def effect(name):
            for offset, value in self.script.writes.get(name, ()):
                self.live.bytes[offset] = value
            for offset, value in self.script.node_writes.get(name, ()):
                self.node[offset] = value
            return -1 if self.fail == name else 0

        def make(name, kinds, fn_type):
            def run(_, *args):
                out, logged, i = None, [], 0
                for kind in kinds:
                    value = args[i]; i += 1
                    if kind == 'p': logged.append(p(value))
                    elif kind == 'f': logged.append(('f', fbits(value)))
                    elif kind == 'I': logged.append(s32(value))
                    else: logged.append(int(value))
                if name == 'drop': logged.append(0x2EC)
                if i < len(args): out = args[i]
                self.log.append((name,) + tuple(logged))
                status = effect(name)
                if out is not None:
                    out[0] = self.script.result(name)
                return status
            fn = fn_type(run)
            self.keep.append(fn)
            return fn

        w.request = make('request', 'piif', REQUEST_FN)
        w.sound = make('sound', 'piif', REQUEST_FN)
        w.cue = make('cue', 'iiii', CUE_FN)
        w.w001EFE00 = make('001EFE00', 'Ip', IDP_FN)
        w.w0015C1F0 = make('0015C1F0', 'p', ACT_FN)
        w.w0021E650 = make('0021E650', 'p', ACT_FN)
        w.w0021D2E0 = make('0021D2E0', 'pii', ACT2_FN)
        w.drop = make('drop', 'p', ACT_FN)
        w.floor = make('floor', 'pi', ACTIR_FN)
        w.translate = make('translate', 'pi', ACTI_FN)
        w.land_sound = make('land_sound', 'pi', ACTI_FN)
        w.w0021D250 = make('0021D250', 'pi', ACTI_FN)
        for name in ('0021D490', '0021C120', '0021C200', '0021C270', '0021C350'):
            setattr(w, 'w' + name, make(name, 'p', ACT_FN))
        w.w0021C190 = make('0021C190', 'p', ACTR_FN)
        w.w0017FC80 = make('0017FC80', 'pf', ACTF_FN)
        w.w0017FF80 = make('0017FF80', 'pf', ACTF_FN)
        w.random = make('random', '', RAND_FN)
        w.w00188550 = make('00188550', 'p', ACTR_FN)
        w.w001885B0 = make('001885B0', 'p', ACTR_FN)

        def root(_, offset, out):
            if self.fail == 'root_node': return -1
            out[0] = self.node[offset]
            return 0
        w.root_node = NODE_FN(root)
        self.keep.append(w.root_node)

    def run(self, raw, scene, node, script, name, args=()):
        self.log = []
        self.script = script
        self.node = dict(node)
        live = self.live = LiveActor()
        C.memmove(live.bytes, bytes(raw), 0x320)
        sc = Scene(scene['d8106F1'], scene['d810707'], scene['d275B14'])
        context = Major2(C.pointer(self.workers), C.pointer(sc))
        if name in STATES:
            ret = getattr(self.lib, 'em_player_major2_' + name)(C.byref(context), C.byref(live))
        elif name == '00181D70':
            ret = self.lib.em_player_major2_00181D70(C.byref(live), C.byref(sc))
        elif name == '001823E0':
            ret = self.lib.em_player_major2_001823E0(C.byref(live))
        else:
            ret = getattr(self.lib, 'em_player_major2_' + name)(C.byref(live), *args)
        return {'bytes': bytes(live.bytes), 'log': self.log, 'ret': ret, 'd8106F1': sc.d8106F1,
                'd810707': sc.d810707, 'd275B14': sc.d275B14, 'node': dict(self.node)}


def build_native():
    out = ROOT / 'build/player_major2_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('major2.dylib' if sys.platform == 'darwin' else 'major2.so')
    sources = ['src/game/em_player_major2.c', 'src/game/em_player_floor.c']
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-shared',
               '-fPIC', '-Isrc'] + sources + ['-lm', '-o', str(lib)]
    digest = hashlib.sha256(' '.join(command).encode())
    for source in sources: digest.update((ROOT / source).read_bytes())
    for header in sorted((ROOT / 'src').rglob('*.h')): digest.update(header.read_bytes())
    stamp = Path(str(lib) + '.sha256')
    if not (lib.exists() and stamp.exists() and stamp.read_text() == digest.hexdigest()):
        subprocess.run(command, cwd=ROOT, check=True)
        stamp.write_text(digest.hexdigest())
    native = C.CDLL(str(lib))
    for name in STATES:
        getattr(native, 'em_player_major2_' + name).argtypes = [C.c_void_p, P]
    native.em_player_major2_00181110.argtypes = [P, C.c_int]
    native.em_player_major2_00181180.argtypes = [P, C.c_int]
    native.em_player_major2_00181D70.argtypes = [P, C.POINTER(Scene)]
    native.em_player_major2_001823E0.argtypes = [P]
    native.em_player_stage_0015B770.argtypes = [C.c_void_p, P]
    return native


# --------------------------------------------------------------- the cases

# Special float patterns: zeros, denormals, Inf/NaN (the EE saturates them).
SPECIAL = (0x00000000, 0x80000000, 0x00000001, 0x807FFFFF, 0x7F800000, 0xFF800000,
           0x7FC00000, 0xFFFFFFFF, 0x7F7FFFFF, 0x00800000)
# Every float immediate these routines compare against (with its neighbours).
LIMITS = (160.0, 20.0, 21.0, 8.0, 35.0, 100.0)


def around(rng, limit):
    b = fbits(limit)
    return rng.choice([b, b - 1, b + 1, fbits(limit * 0.5), fbits(limit * 2), fbits(-limit)])


def random_float(rng):
    r = rng.random()
    if r < 0.15: return rng.choice(SPECIAL)
    if r < 0.45: return around(rng, rng.choice(LIMITS))
    if r < 0.6: return fbits(0.0)
    return fbits(rng.uniform(-400.0, 400.0))


def put(raw, offset, word):
    struct.pack_into('<I', raw, offset, word & 0xFFFFFFFF)


def random_record(rng, seeds):
    if seeds and rng.random() < 0.4:
        raw = bytearray(rng.choice(seeds))
    else:
        raw = bytearray(rng.randrange(256) for _ in range(0x320))
    for offset in (0x3C, 0x220, 0x224, 0x228, 0x22C, 0x21C, 0x2E4, 0xB4, 0x2EC, 0x294, 0x290, 0x298):
        put(raw, offset, random_float(rng))
    put(raw, 0x200, rng.choice([0, 0x1000, 0x8000, 0x9000, rng.randrange(1 << 32)]))
    raw[0xF] = rng.choice([0, 2, 0x63, 0x61, 0x63 | 2, rng.randrange(256)])
    raw[0x234] = rng.choice([0, 1, 2, rng.randrange(256)])
    raw[0x2F1] = rng.choice([0, 1, 2, rng.randrange(256)])
    raw[0x302] = rng.choice([0, 1, 9, rng.randrange(256)])
    raw[0x23A] = rng.choice([0x5D, 0, 0x5A, rng.randrange(256)])
    struct.pack_into('<h', raw, 0x28, rng.choice([0, 1, 2, -1, 0x10, rng.randrange(-32768, 32768)]))
    return raw


SUBSTATES = {  # the switch labels of each routine (anything else is its default)
    '0021E830': (0, 1, 2, 3), '00221FC0': (0, 1, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0x10, 0x14, 0x15, 0x16),
    '00222580': (0, 1, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0x14, 0x15, 0x16),
    '00222AD0': (0, 1, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0x14, 0x15, 0x16),
    '002230A0': (0, 1, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0x10, 0x14, 0x15, 0x16),
    '00225570': (0, 1), '002255C0': (0, 1, 2),
}


# The bytes the routines read or write, in two groups: control bytes, and
# the high/low bytes of the floats (a clobbered gauge would otherwise route
# every case into the knock-down).
CLOBBER = ((6, 7, 0xF, 0x28, 0x29, 0x201, 0x203, 0x234, 0x23A, 0x2F1, 0x302),
           (0x3F, 0xB7, 0x21F, 0x223, 0x227, 0x22B, 0x22F, 0x2E7, 0x2EF, 0x290, 0x293, 0x294, 0x297,
            0x298, 0x29B))


def random_script(rng):
    names = ('request', 'sound', 'cue', 'translate', 'floor', 'drop', 'land_sound', '0021C190',
             '001885B0', '00188550', '0021C350', '0021D490', 'random')
    writes, node_writes = {}, {}
    if rng.random() < 0.5:
        for name in rng.sample(names, rng.randrange(1, 4)):
            writes[name] = [(rng.choice([6, 7, 0xF, 0x234, 0x2F1, 0x23A, 0x294, 0x295, 0x297, 0x2EC,
                                         0x2EF, 0xB4, 0xB7, 0x3C, 0x3F, 0x220, 0x223, 0x302, 0x28]),
                             rng.randrange(256))]
    if rng.random() < 0.5:
        node_writes[rng.choice(['translate', 'floor', 'drop'])] = [(rng.choice([4, 8]), random_float(rng))]
    results = {'floor': [rng.choice([0, 1, 2, -1])], '0021C190': [rng.choice([0, 1, 5])],
               'random': [rng.randrange(1 << 31)], '00188550': [rng.choice([0x10, 0x8C, 0x1C8, -1])],
               '001885B0': [rng.choice([0xDD, 0x60, 0x7FFF])]}
    return Script(results, writes, node_writes)


def random_scene(rng):
    return {'d8106F1': rng.choice([0, 1, 0xFF]), 'd810707': rng.choice([0, 1, 3]),
            'd275B14': rng.choice([0x1E, 0x36, 0x34, 0, -1, rng.randrange(-5, 100)])}


def classify(name, raw, result):
    """A path class (from the inputs and the recorded original run)."""
    log = [e[0] for e in result['log']]
    out = result['bytes']
    if name in ENTRIES:
        return (name, result['ret'], result['d275B14'] if name == '00181D70' and result['ret'] else None)
    st = raw[6]
    label = st if st in SUBSTATES[name] else 'default'
    tag = None
    if name == '0021E830':
        tag = (out[6] != st) if st in (1, 2) else None
    elif name == '002255C0':
        if st == 0: tag = ('hit' if '0021C350' in log else 'quiet', '001FBD50 0x156' if
                           any(e[0] == 'sound' and e[2] == 0x156 for e in result['log']) else 'hold')
        elif st == 1: tag = 'advance' if '0021D490' in log else 'count'
    elif name == '00225570':
        tag = None
    elif st == 0:
        clips = [e[2] for e in result['log'] if e[0] == 'request']
        tag = ('clip', clips[0]) if clips else ('knockdown', out[6])
        if out[6] == 0x14: tag = 'alt'
    elif st in (1, 0x16):
        tag = ('exit', out[4], out[5], out[6]) if out[4] == 1 else 'wait'
    elif 'land_sound' in log:
        tag = 'next' if 'request' in log else 'reset'
    elif 'drop' in log:
        tag = '5d' if '0021D250' in log else 'air'
    elif 'translate' in log:
        tag = 'root'
    return (name, label, tag)


LAND = {'00221FC0': 0xD, '00222580': 0xC, '00222AD0': 0xC, '002230A0': 0xD}
NEED = {  # branch classes every run must reach, beyond every switch label
    '00221FC0': {(0, ('clip', c)) for c in (0x102, 0x103, 0x104, 0x105)} |
                {(0, ('knockdown', 0xA)), (0, 'alt'), (1, ('exit', 1, 0xC, 0)), (0x16, ('exit', 1, 0xC, 0)),
                 (0xD, 'reset'), (0xD, 'next'), (0xD, '5d'), (0xD, 'air'), (0xC, 'root')},
    '00222580': {(0, ('clip', c)) for c in (0xB5, 0xB6, 0xB7, 0xB8)} |
                {(0, ('knockdown', 0xA)), (0, 'alt'), (1, ('exit', 1, 0xE, 0)), (0x16, ('exit', 1, 0xE, 0)),
                 (0xC, 'reset'), (0xC, 'next'), (0xC, 'air'), (0xB, 'root')},
    '00222AD0': {(0, ('clip', c)) for c in (0x8F, 0x90, 0x1C8)} |
                {(0, ('knockdown', 0xA)), (0, 'alt'), (1, ('exit', 1, 9, 0)), (1, ('exit', 1, 0x18, 0)),
                 (0x16, ('exit', 1, 9, 0)), (0x16, ('exit', 1, 0x18, 0)),
                 (0xC, 'reset'), (0xC, 'next'), (0xC, '5d'), (0xC, 'air'), (0xB, 'root')},
    '002230A0': {(0, ('clip', c)) for c in (0xDD, 0x1C9, 0xDF, 0xE0)} |
                {(0, ('knockdown', 0xA)), (0, 'alt'), (1, ('exit', 1, 0x12, 0)), (1, ('exit', 1, 0x12, 0x28)),
                 (1, ('exit', 1, 0x10, 0)), (0x16, ('exit', 1, 0x12, 0)), (0x16, ('exit', 1, 0x10, 0)),
                 (0xD, 'reset'), (0xD, 'next'), (0xD, '5d'), (0xD, 'air'), (0xC, 'root')},
    '0021E830': {(1, True), (1, False), (2, True), (2, False)},
    '002255C0': {(0, ('hit', 'hold')), (0, ('quiet', '001FBD50 0x156')), (1, 'advance'), (1, 'count')},
}


def required_paths():
    need = set()
    for name, labels in SUBSTATES.items():
        for label in labels + ('default',):
            need.add((name, label))
    return need


def compare(where, want, got):
    assert got['log'] == want['log'], (where, 'callees', got['log'], want['log'])
    for k in range(0x320):
        assert got['bytes'][k] == want['bytes'][k], (where, hex(k), got['bytes'][k], want['bytes'][k])
    for key in ('ret', 'd8106F1', 'd810707', 'd275B14', 'node'):
        assert got[key] == want[key], (where, key, got[key], want[key])


# Deterministic scenarios: the branches a random draw reaches rarely.
def scenarios():
    out = []
    zero, one = fbits(0.0), fbits(1.0)

    def sc(name, sub, fields=None, bytes_=None, results=None, scene=None, args=(), clobber=False):
        out.append((name, sub, fields or {}, bytes_ or {}, results or {}, scene or {}, args, clobber))
    for name in ('00221FC0', '00222580', '00222AD0', '002230A0'):
        # state 0: each clip, the knock-down, the alternate exit (and its gate)
        for f2f1, alt in ((0, 0), (0, 2), (1, 0), (1, 2), (2, 0)):
            sc(name, 0, {0x220: one, 0x228: zero, 0x224: zero, 0x22C: zero},
               {0xF: alt, 0x2F1: f2f1, 0x302: 0, 0x234: f2f1 & 1})
        sc(name, 0, {0x220: 0x80000000, 0x224: one, 0x22C: one})           # -0 knocks down
        sc(name, 0, {0x220: 0x00000001, 0x224: 0x80000001})                 # denormal = 0
        sc(name, 0, {0x220: one, 0x228: fbits(100.0)}, scene={'d8106F1': 1})
        sc(name, 0, {0x220: one, 0x228: fbits(100.0)}, scene={'d8106F1': 0})
        sc(name, 0, {0x220: one, 0x228: fbits(99.99999)}, scene={'d8106F1': 1})
        for sub in (1, 0x16):
            for flags in (0x1000, 0):
                for step in (9, 0):
                    for timer in (0x1E, 0x36, 0x34):
                        sc(name, sub, {0x200: flags}, {0x302: step}, scene={'d275B14': timer})
        land = 0xD if name in ('00221FC0', '002230A0') else 0xC
        for floor in (0, 1):
            for f, b234, surface in ((0x63, 0, 0), (0, 1, 0), (0, 0, 0x5D), (0, 0, 0)):
                sc(name, land, bytes_={0xF: f, 0x234: b234, 0x23A: surface},
                   results={'floor': [floor]})
        root = land - 1
        sc(name, root, {0x200: 0x1000})
        sc(name, root, {0x200: 0, 0x21C: fbits(3.5), 0x2E4: fbits(-2.25), 0xB4: fbits(10.0)})
        first = 0x14
        for limit in (8.0, 20.0, 21.0, 35.0):
            for bits_ in (fbits(limit), fbits(limit) + 1):
                sc(name, first, {0x3C: bits_})
                sc(name, land + 1, {0x3C: bits_})
        for done in (0, 1):
            sc(name, 0x15, results={'0021C190': [done]})
        sc(name, 0xB, {0x200: 0x8000}); sc(name, 0xB, {0x200: 0})
        sc(name, land + 2, {0x200: 0x1000}); sc(name, land + 2, {0x200: 0})
    for st in (0, 1, 2, 3, 4, 0xFF):
        sc('0021E830', st, {0x3C: fbits(160.0), 0x200: 0x1000})
        sc('0021E830', st, {0x3C: fbits(160.0) + 1, 0x200: 0})
    for st in (0, 1, 2, 3):
        sc('00225570', st)
    for hit in (zero, one):
        for gauge in (zero, one):
            sc('002255C0', 0, {0x224: hit, 0x220: gauge})
    for count in (0, 1, -1, 0x10):
        sc('002255C0', 1, bytes_={0x28: count & 0xFF, 0x29: (count >> 8) & 0xFF})
    sc('002255C0', 2); sc('002255C0', 3)
    # Read-after-call order: one callee at a time overwrites the bytes the
    # routines read and write (and the skeleton node), so a read or a store
    # moved across that call shows.
    k = 0
    for name, labels in SUBSTATES.items():
        for sub in labels:
            for callee, _ in CALLEES.values():
                for flags in (0x1000, 0):
                    for group in CLOBBER:
                        k += 1
                        sc(name, sub, {0x200: flags, 0x220: fbits(1.0), 0x228: 0, 0x3C: fbits(4.0)},
                           results={'floor': [k & 1], '0021C190': [k >> 1 & 1]}, clobber=(callee, group))
    # Every float compare against every special operand (zeros, denormals,
    # Inf/NaN of both signs): the EE's DAZ and sign-keeping saturation.
    one = fbits(1.0)
    for special in SPECIAL:
        for name in ('00221FC0', '00222580', '00222AD0', '002230A0'):
            sc(name, 0, {0x220: one, 0x228: special}, scene={'d8106F1': 1})
            sc(name, 0, {0x220: special, 0x224: special, 0x22C: special})
            for sub in (0xD, 0xE, 0x14):
                sc(name, sub, {0x3C: special})
        sc('0021E830', 1, {0x3C: special})
        sc('002255C0', 0, {0x224: special, 0x220: special})
        for name in ENTRIES:
            sc(name, 0, {0x224: special, 0x22C: special}, args=(1,) if name in ('00181110', '00181180') else ())
    # Entry routines: each guard term, both returns, each D_00275B14 value.
    for name in ENTRIES:
        for a224, a22c, f in ((zero, zero, 0), (0x80000000, 0x00000001, 0), (one, zero, 0),
                              (zero, one, 0), (zero, zero, 2)):
            for five, six in ((0x10, 0), (0, 0x28), (0, 0x27)):
                sc(name, six, {0x224: a224, 0x22C: a22c}, {0xF: f, 5: five},
                   args=(rng_step(name),) if name in ('00181110', '00181180') else ())
    return out


def rng_step(name):
    return 0x1FF if name == '00181110' else 1


def one_case(oracle, native, rng, seeds, spec, index):
    """Run one case on both sides; returns its path class."""
    if spec is None:
        name = rng.choice(list(STATES) * 3 + list(ENTRIES))
        raw = random_record(rng, seeds)
        if name in STATES:
            labels = SUBSTATES[name]
            raw[6] = rng.choice(labels + labels + (rng.randrange(256),))
        script = random_script(rng)
        scene = random_scene(rng)
        args = (rng.choice([0, 1, 0x1FF, -1]),) if name in ('00181110', '00181180') else ()
    else:
        name, sub, fields, bytes_, results, scene_fields, args, clobber = spec
        raw = random_record(rng, seeds)
        raw[6] = sub
        for offset, word in fields.items(): put(raw, offset, word)
        for offset, value in bytes_.items(): raw[offset] = value
        script = Script(dict({'floor': [1], '0021C190': [0], 'random': [rng.randrange(1 << 31)],
                              '00188550': [0x8C], '001885B0': [0xDD]}, **results), {}, {})
        scene = dict(random_scene(rng), **scene_fields)
        if clobber:
            callee, group = clobber
            script.writes[callee] = [(offset, rng.randrange(256)) for offset in group]
            script.node_writes[callee] = [(4, random_float(rng)), (8, random_float(rng))]
    node = {4: random_float(rng), 8: random_float(rng)}
    if name in STATES: raw[4], raw[5] = 2, SLOT[name]
    address = STATES.get(name) or ENTRIES[name]
    script.count = {}
    want = oracle.run(raw, scene, node, script, address, args)
    script.count = {}
    got = native.run(raw, scene, node, script, name, args)
    if name in STATES:   # void originals: $v0 is not a result; the native reports success
        assert got['ret'] == 0, (index, name, got['ret'])
        want = dict(want, ret=0)
    compare((index, name, raw[6]), want, got)
    return classify(name, raw, want), (name, {e[0] for e in want['log']})


def dispatch_section(oracle, native, rng, seeds, count):
    """0015B770 itself: the original dispatcher running the original routines
    against em_player_stage_0015B770 with the native routines bound into
    state2[3..7, 0x16, 0x19] (the binding the coordinator makes)."""
    lib = native.lib
    stage_workers = StageWorkers()
    keep = []

    def stub(tag):
        def run(_, actor):
            native.log.append(tag)
            return 0
        fn = STATE_FN(run); keep.append(fn); return fn

    def reaction(_, actor, out):
        native.log.append(('reaction', 'p')); out[0] = 0; return 0
    stage_workers.reaction = RESULT_FN(reaction); keep.append(stage_workers.reaction)
    for slot, address in OTHER_STATE2.items(): stage_workers.state2[slot] = stub((('state', address), 'p'))
    for i, address in enumerate(PHASE13): stage_workers.phase13[i] = stub((('state', address), 'p'))
    for i, address in enumerate(PHASE14): stage_workers.phase14[i] = stub((('state', address), 'p'))
    scene = Scene()
    context = Major2(C.pointer(native.workers), C.pointer(scene))
    for name, slot in SLOT.items():
        stage_workers.state2[slot] = C.cast(getattr(lib, 'em_player_major2_' + name), STATE_FN)
        stage_workers.state2_context[slot] = C.addressof(context)
    stage = Stage(C.pointer(StageScene()), C.pointer(stage_workers))
    seen = set()
    fives = sorted(set(SLOT.values())) + [8, 0xD, 0xE, 0x1A, 0xFF] + sorted(OTHER_STATE2)
    for i in range(count):
        raw = random_record(rng, seeds)
        raw[4] = 2
        raw[5] = fives[i] if i < len(fives) else rng.choice(fives)
        raw[0xD] = rng.randrange(6)
        name = next((n for n, s in SLOT.items() if s == raw[5]), None)
        if name: raw[6] = rng.choice(SUBSTATES[name])
        raw[1] = rng.randrange(256)
        script, sc = random_script(rng), random_scene(rng)
        node = {4: random_float(rng), 8: random_float(rng)}
        script.count = {}
        want = oracle.run(raw, sc, node, script, DISPATCH)
        # native: the same script through the stage table
        script.count = {}
        native.log, native.script, native.node = [], script, dict(node)
        live = native.live = LiveActor()
        C.memmove(live.bytes, bytes(raw), 0x320)
        scene.d8106F1, scene.d810707, scene.d275B14 = sc['d8106F1'], sc['d810707'], sc['d275B14']
        assert lib.em_player_stage_0015B770(C.addressof(stage), C.byref(live)) == 0
        got = {'bytes': bytes(live.bytes), 'log': native.log, 'ret': want['ret'], 'd8106F1': scene.d8106F1,
               'd810707': scene.d810707, 'd275B14': scene.d275B14, 'node': dict(native.node)}
        compare(('0015B770', i, raw[5]), want, got)
        seen.add(raw[5])
    for s in set(SLOT.values()) | {0x19}:
        assert s in seen, ('0015B770 slot not reached', s)
    return len(seen)


FIELD_OF = {'request': 'request', 'sound': 'sound', 'cue': 'cue', 'drop': 'drop', 'floor': 'floor',
            'translate': 'translate', 'land_sound': 'land_sound', 'random': 'random'}


def fault_section(native, rng, seeds, reached):
    """Fail-stop. For each routine, removing any one worker the ORIGINAL was
    seen to call (over every case run above; root_node wherever 00178B90 was
    reached, since the root reads sit beside it) faults with -1 before any
    write, and removing any other worker does not. A worker returning a fault
    stops the routine with -1."""
    checked = 0
    for name in STATES:
        expected = {FIELD_OF.get(callee, 'w' + callee) for callee in reached[name]}
        if 'translate' in expected: expected.add('root_node')
        raw = random_record(rng, seeds)
        raw[6] = 0xFF   # a default sub-state: the fewest writes, so a fault shows as "no write"
        faulted = set()
        for field, fn_type in WORKER_FIELDS:
            partial = Workers.from_buffer_copy(native.workers)
            setattr(partial, field, fn_type())
            live = LiveActor(); C.memmove(live.bytes, bytes(raw), 0x320)
            sc = Scene()
            context = Major2(C.pointer(partial), C.pointer(sc))
            native.script, native.live, native.log = Script({}, {}, {}), live, []
            r = getattr(native.lib, 'em_player_major2_' + name)(C.byref(context), C.byref(live))
            if r == -1:
                assert bytes(live.bytes) == bytes(raw) and native.log == [], (name, field, 'acted before faulting')
                faulted.add(field)
            else:
                assert r == 0, (name, field, r)
            checked += 1
        assert faulted == expected, (name, sorted(faulted ^ expected))
        live = LiveActor()
        assert getattr(native.lib, 'em_player_major2_' + name)(None, C.byref(live)) == -1
    raw = random_record(rng, seeds); raw[6] = 0; put(raw, 0x220, fbits(1.0))
    native.fail = 'cue'
    got = native.run(raw, random_scene(rng), {4: 0, 8: 0}, Script({}, {}, {}), '00221FC0')
    native.fail = None
    assert got['ret'] == -1 and [e[0] for e in got['log']] == ['cue'], got
    assert native.lib.em_player_major2_00181D70(C.byref(LiveActor()), None) == -1
    assert native.lib.em_player_major2_001823E0(None) == -1
    return checked


def main():
    elf = read_elf()
    if not WORLD_RAM.exists():
        print('player major2 reference: SKIP (missing %s)' % WORLD_RAM)
        return 0
    ram = WORLD_RAM.read_bytes()
    assert len(ram) == 0x2000000, 'unexpected RAM image size'
    for address, size in SIZES.items():
        at = address - 0x100000 + 0x300
        assert ram[address:address + size] == elf[at:at + size], ('captured code differs', hex(address))
    seeds = [ram[PLAYER:PLAYER + 0x320]]
    for path in SEED_CAPTURES:
        if path.exists():
            with open(path, 'rb') as handle:
                handle.seek(PLAYER)
                seeds.append(handle.read(0x320))
    native_lib = build_native()
    oracle = Oracle(ram)
    del ram
    native = Native(native_lib)
    rng = random.Random(0x2A2)
    paths = set()
    reached = {name: set() for name in STATES}
    fixed = scenarios()
    bulk = reference_mode.pick(30000, 1500)
    for i, spec in enumerate(fixed + [None] * bulk):
        path, (name, callees) = one_case(oracle, native, rng, seeds, spec, i)
        paths.add(path)
        if name in reached: reached[name] |= callees
    labels = {(p[0], p[1]) for p in paths if p[0] in STATES}
    missing = required_paths() - labels
    assert not missing, sorted(map(str, missing))
    entry_paths = {p for p in paths if p[0] in ENTRIES}
    for name in ENTRIES:
        assert {0, 1} <= {p[1] for p in entry_paths if p[0] == name}, (name, 'both returns')
    for name, need in NEED.items():
        have = {(p[1], p[2]) for p in paths if p[0] == name}
        assert need <= have, (name, sorted(map(str, need - have)))
    timers = {p[2] for p in entry_paths if p[0] == '00181D70' and p[1] == 1}
    assert timers >= {0x34, 0x36, 0x1E}, timers
    dispatched = dispatch_section(oracle, native, rng, seeds, reference_mode.pick(3000, 120))
    faults = fault_section(native, rng, seeds, reached)
    reference_mode.banner('%d fixed scenarios' % len(fixed),
                          reference_mode.part(bulk, 30000, 'random cases'),
                          '%d path classes' % len(paths),
                          '%d 0015B770 slots' % dispatched, '%d fail-stop checks' % faults)
    print('player major2 reference: PASS (original 0021E830/00221FC0/00222580/00222AD0/002230A0/'
          '00225570/002255C0/00181110/00181180/00181D70/001823E0/0015B770 instructions, '
          'COP1 through ee_float_model)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
