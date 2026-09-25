#!/usr/bin/env python3
"""Execute the original 0017BC40 and 0017B910 over a player record; compare
their record-level translations (em_player_motor_0017BC40,
em_player_foot_stop_0017B910).

docs/LOCOMOTION_DISPLAY.md section 4 (the `motor` and `foot_stop` workers of
the idle / walk states). The user's pinned ELF supplies every instruction
and table; none are embedded here.

0017BC40 runs with no callee. The native side reads the tier tables through
a reader over the ELF's exported span 0x248740..0x248ACC (what the live
binder maps). Every case compares all 0x320 record bytes and 0x70003A20.

0017B910's callees anim_eval_skeleton, 0017B490, 001C61D0 and 001749A0 are
hooked on the original side (scripted results, recorded arguments) and the
native workers apply the same script; the SDK sqrtf 0011E748 and the VU0
leaves 001029C0 / 00102C58 / 001026A0 run as original instructions on the
original side, and as their verified translations (em_sdk_math_original,
em_owner_services_original, em_effect_original) on the native side. Every
case compares the record, the scratchpad words 0x70003A20..2C,
0x700036A0..DF and 0x700038A0..BF, and the worker calls with their arguments.

Arithmetic: tools/ee_float_model.py through test_coll_move_reference.FloatEE.
Default run: a quick sample; EM_TEST_FULL=1: the whole sweep.
"""
import ctypes as C
import hashlib
import random
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import reference_mode  # noqa: E402
import test_player_slide_reference as SR  # noqa: E402
from test_coll_move_reference import FloatEE  # noqa: E402

LANE = ROOT / 'build' / 'player_loco_workers'
MOTOR, FOOT = 0x17BC40, 0x17B910
EVAL, SELECT, FRAMES, REQUEST = 0x1C6DA0, 0x17B490, 0x1C61D0, 0x1749A0
SPAN = (0x248740, 0x248ACC)
ACTOR = 0x01E00000
NODE17, NODE18 = 0x01E10000, 0x01E11000
D_B40, D_MODE = 0x275B40, 0x26C5D0
MASK = 0xFFFFFFFF

SOURCES = ('src/game/em_player_motor.c', 'src/game/em_player_foot_stop.c',
           'src/game/em_camera_rotation.c', 'src/game/em_effect_original.c',
           'src/game/em_owner_services_original.c', 'src/game/em_sdk_math_original.c',
           'src/game/em_anim_runtime_rest.c', 'src/game/em_pose_host_workers.c',
           'src/game/em_stream_lanes_original.c', 'src/game/em_player_stage_workers.c',
           'src/game/em_player_reaction.c', 'src/game/em_player_floor.c', 'src/game/em_player_fall.c',
           'src/game/em_script_host_workers.c', 'src/game/em_script.c')


def F(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def sx16(value):
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def build():
    LANE.mkdir(parents=True, exist_ok=True)
    lib = LANE / ('loco_workers' + ('.dylib' if sys.platform == 'darwin' else '.so'))
    command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
               '-shared', '-fPIC', '-Isrc', *SOURCES, '-lm', '-o', str(lib)]
    digest = hashlib.sha256(' '.join(command).encode())
    for path in [ROOT / s for s in SOURCES] + sorted((ROOT / 'src').rglob('*.h')):
        digest.update(path.read_bytes())
    stamp = Path(str(lib) + '.sha256')
    if not (lib.exists() and stamp.exists() and stamp.read_text() == digest.hexdigest()):
        import subprocess
        subprocess.run(command, cwd=ROOT, check=True)
        stamp.write_text(digest.hexdigest())
    return C.CDLL(str(lib))


READ = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32))
ACTOR_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p)
SELECT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, C.c_int, C.c_int, C.c_int,
                        C.POINTER(C.c_int16))
FRAMES_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_int, C.POINTER(C.c_int32))
REQUEST_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, C.c_int, C.c_int, C.c_float)
SQRT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32))


class Actor(C.Structure):
    _fields_ = [('bytes', C.c_uint8 * 0x320), ('link_owner', C.c_void_p), ('link_prev', C.c_void_p),
                ('link_flags', C.c_uint8), ('link_type', C.c_uint8)]


class FootWorkers(C.Structure):
    _fields_ = [('context', C.c_void_p), ('eval_skeleton', ACTOR_FN), ('select', SELECT_FN),
                ('clip_frames', FRAMES_FN), ('request', REQUEST_FN), ('sqrt', SQRT_FN), ('read', READ)]


class FootScratch(C.Structure):
    _fields_ = [(name, C.POINTER(C.c_uint32)) for name in ('s3A20', 's3A24', 's36A0', 's38A0', 's38B0')]


class SdkContext(C.Structure):
    _fields_ = [('tables', C.c_void_p), ('d26C5D0', C.c_void_p), ('wctx', C.c_void_p), ('w0', C.c_void_p),
                ('w1', C.c_void_p), ('w2', C.c_void_p), ('w3', C.c_void_p), ('fault', C.c_uint32)]


def span_reader(base):
    def read(_, address, out):
        if not (SPAN[0] <= address and address + 4 <= SPAN[1]) or address & 3:
            return -1
        out[0] = struct.unpack_from('<I', base, address)[0]
        return 0
    return READ(read)


# ---------------------------------------------------------------- 0017BC40

SPEEDS = (0.0, -0.0, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.3625, 0.425, 0.5, 0.8, 0.9)


def motor_case(rng, index):
    record = bytearray(0x320)
    for at in range(0, 0x320, 4):
        struct.pack_into('<I', record, at, rng.getrandbits(32) if rng.random() < 0.3 else 0)
    record[0x1F0] = index % 9 if index < 400 else rng.randrange(9)
    record[0x1F1] = rng.randrange(4)
    record[0x25C] = rng.choice((0, 1, 2, 3, 0, 1, 2, 3, 4, 7))
    record[0x23F] = rng.randrange(4)
    record[0x314] = rng.choice((0, 0, 1, 0x20, 0x1F, 0xFF))
    speed = rng.choice(SPEEDS) if rng.random() < 0.8 else rng.uniform(-0.2, 1.2)
    target = rng.choice((0.0, -0.0, 0.1, 0.3, 0.8, speed)) if rng.random() < 0.9 else rng.uniform(0, 1)
    struct.pack_into('<f', record, 0x38, speed)
    struct.pack_into('<f', record, 0x240, target)
    struct.pack_into('<f', record, 0x204, rng.choice((0.0, 0.75, 1.0, 1.5)))
    struct.pack_into('<f', record, 0x208, rng.choice((0.0, 0.5, 1.0)))
    return bytes(record)


def motor_grid():
    """Every mode x sub-mode x tier with the speed below, at and above the
    target, the target zero, and the gait zero or not: each branch of
    0017BC40 both ways."""
    out = []
    for mode in range(9):
        for sub in range(4):
            for tier in range(4):
                for speed, target in ((0.1, 0.3), (0.3, 0.3), (0.8, 0.3), (0.3, 0.0), (0.0, 0.8),
                                      (0.3625, 0.8), (0.12, 0.1), (0.02, 0.0)):
                    for gait in (0, 2):
                        for lanes in (0, 4):
                            record = bytearray(0x320)
                            record[0x1F0], record[0x1F1], record[0x25C] = mode, sub, tier
                            record[0x23F], record[0x314] = gait, lanes
                            struct.pack_into('<f', record, 0x38, speed)
                            struct.pack_into('<f', record, 0x240, target)
                            struct.pack_into('<f', record, 0x204, 1.0)
                            out.append(bytes(record))
    return out


def check_motor(lib, base, cases):
    reader = span_reader(base)
    lib.em_player_motor_0017BC40.argtypes = [C.c_void_p, READ, C.c_void_p, C.POINTER(C.c_uint32)]
    faults = 0
    ee = FloatEE(ELF_BYTES, ram=base, spad=bytes(0x4000))
    for record in cases:
        ee.r = [0] * 32
        ee.r[28], ee.r[29] = 0x27D370, SR.STACK_TOP
        ee.write(ACTOR, record)
        ee.save(0x70003A20, 0xA5A5A5A5)
        ee.call(MOTOR, (ACTOR,))
        want = ee.read(ACTOR, 0x320)
        want3A20 = ee.load(0x70003A20)
        buf = (C.c_uint8 * 0x320).from_buffer_copy(record)
        s3A20 = C.c_uint32(0xA5A5A5A5)
        status = lib.em_player_motor_0017BC40(buf, reader, None, C.byref(s3A20))
        if status < 0:
            # Only a table word outside the exported span; nothing written.
            faults += 1
            assert bytes(buf) == record and s3A20.value == 0xA5A5A5A5
            continue
        assert bytes(buf) == want, ('0017BC40 record', record[0x1F0], record[0x1F1], record[0x25C],
                                    [hex(i) for i in range(0x320) if buf[i] != want[i]])
        assert s3A20.value == want3A20, ('0017BC40 0x70003A20', hex(s3A20.value), hex(want3A20))
    return faults


# ---------------------------------------------------------------- 0017B910

def foot_case(rng):
    c = {
        'row': rng.choice((0, 0, 0, 1)), 'busy': rng.choice((0, 0, 0, 0, 1)),
        'tier': rng.choice((1, 1, 2, 2, 0, 3)),
        'clock': rng.choice((0.0, 0.5, 1.0, 1.5, 12.0, 23.999, 24.0, 40.0, 57.999, 58.0, 90.0, 119.0))
                 if rng.random() < 0.5 else rng.uniform(-2, 125),
        'frames': rng.choice((120, 45, 60, 1, 0, 200)),
        'clip1': rng.choice((1, 2, 3, 0x7FFF, 0x8001)), 'clip6': rng.choice((4, 5, 0xFFFF)),
        'clip0': rng.choice((0, 0, 0x0A, 3)), 'current': rng.choice((0, 0, 0x0A, 1)),
        'flags': rng.choice((0, 0x8000, 0x9000, 0x1000)),
        'pos': [rng.uniform(-400, 400) for _ in range(3)],
        'euler': [rng.uniform(-3.2, 3.2) if rng.random() < 0.3 else 0.0, rng.uniform(-3.2, 3.2),
                  rng.uniform(-0.3, 0.3) if rng.random() < 0.3 else 0.0],
        'bank': rng.getrandbits(32),
    }
    c['feet'] = [[c['pos'][k] + rng.uniform(-12, 12) for k in range(3)] for _ in range(2)]
    return c


class NativeView:
    """The native side's EE words: the case's record and nodes, else the ELF image."""

    def __init__(self, c):
        fake = _Fake()
        lay_out(fake, c)
        self.words = fake.words
        self.record = bytes(fake.record)

    def word(self, address, base):
        if address in self.words:
            return self.words[address]
        if address + 4 > len(base):
            return None
        return struct.unpack_from('<I', base, address)[0]


class _Fake:
    """A write sink for lay_out: word stores by EE address."""

    def __init__(self):
        self.words = {}
        self.record = bytearray(0x320)

    def __setitem__(self, key, value):
        assert key == slice(ACTOR, ACTOR + 0x320)
        self.record[:] = value

    def pack(self, fmt, at, *values):
        data = struct.pack(fmt, *values)
        for k in range(0, len(data), 4):
            self.words[at + k] = struct.unpack_from('<I', data, k)[0]


def lay_out(ram, c):
    """The record, D_00275B40 (the record's +110 array) and nodes 17 / 18."""
    rec = bytearray(0x320)
    rec[0x235], rec[0x236], rec[0x25C] = c['row'], c['busy'], c['tier']
    struct.pack_into('<f', rec, 0x3C, c['clock'])
    struct.pack_into('<I', rec, 0x40, c['bank'])
    struct.pack_into('<3f', rec, 0xB0, *c['pos'])
    struct.pack_into('<3f', rec, 0xC0, *c['euler'])
    struct.pack_into('<H', rec, 0x20C, c['current'])
    struct.pack_into('<I', rec, 0x200, c['flags'])
    struct.pack_into('<I', rec, 0x110 + 17 * 4, NODE17)
    struct.pack_into('<I', rec, 0x110 + 18 * 4, NODE18)
    ram[ACTOR:ACTOR + 0x320] = rec
    put = ram.pack if isinstance(ram, _Fake) else (lambda fmt, at, *v: struct.pack_into(fmt, ram, at, *v))
    put('<I', D_B40, ACTOR + 0x110)
    for node, foot in ((NODE17, c['feet'][0]), (NODE18, c['feet'][1])):
        put('<3f', node + 0xC0, *foot)


def check_foot(lib, base, cases):
    lib.em_player_foot_stop_0017B910.argtypes = [C.POINTER(FootWorkers), C.POINTER(FootScratch),
                                                 C.POINTER(C.c_uint32), C.POINTER(Actor)]
    lib.em_anim_rest_sqrt_0011E748.argtypes = [C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32)]
    d26 = C.c_int32(struct.unpack_from('<i', base, D_MODE)[0])
    sdk = SdkContext(C.addressof(SDK_TABLES), C.addressof(d26))
    calls_total = 0
    ee = FloatEE(ELF_BYTES, ram=base, spad=bytes(0x4000))
    ram = ee.mem
    for c in cases:
        ee.r = [0] * 32
        ee.r[28], ee.r[29] = 0x27D370, SR.STACK_TOP
        ee.spad[:] = bytes(0x4000)
        for at in (ACTOR, NODE17, NODE18):
            ram[at:at + 0x400] = bytes(0x400)
        lay_out(ram, c)
        for at in range(0x3A20, 0x3A30, 4): ee.save(0x70000000 + at, 0x5A5A5A5A)
        log = []

        def select(o):
            cmd = o.r[5] & MASK
            log.append(('select', o.r[4] & MASK, cmd, o.r[6] & MASK, o.r[7] & MASK))
            value = {1: c['clip1'], 6: c['clip6'], 0: c['clip0']}[cmd]
            o.r[2] = sx16(value)

        def frames(o):
            log.append(('frames', o.r[4] & MASK, sx16(o.r[5])))
            o.r[2] = c['frames']

        ee.hooks = {EVAL: lambda o: log.append(('eval', o.r[4] & MASK)), SELECT: select,
                    FRAMES: frames,
                    REQUEST: lambda o: log.append(('request', o.r[4] & MASK, sx16(o.r[5]), o.r[6] & MASK,
                                                   o.f[12]))}
        ee.call(FOOT, (ACTOR,))
        want_rec = ee.read(ACTOR, 0x320)
        want_spad = {name: ee.read(0x70000000 + at, size) for name, at, size in
                     (('s3A20', 0x3A20, 4), ('s3A24', 0x3A24, 12), ('s36A0', 0x36A0, 64),
                      ('s38A0', 0x38A0, 16), ('s38B0', 0x38B0, 16))}

        # The native side over the same starting memory (the routine only
        # reads outside the record: the D_0024875C row and the node words).
        nram = NativeView(c)
        actor = Actor()
        C.memmove(actor.bytes, nram.record, 0x320)
        native_log = []

        def n_eval(_, a):
            native_log.append(('eval', ACTOR)); return 0

        def n_select(_, a, cmd, idx, tbl, clip):
            native_log.append(('select', ACTOR, cmd & MASK, idx & MASK, tbl & MASK))
            clip[0] = sx16({1: c['clip1'], 6: c['clip6'], 0: c['clip0']}[cmd])
            return 0

        def n_frames(_, bank, clip, out):
            native_log.append(('frames', bank & MASK, clip)); out[0] = c['frames']; return 0

        def n_request(_, a, clip, flags, blend):
            native_log.append(('request', ACTOR, clip, flags & MASK, F(blend))); return 0

        def n_read(_, address, out):
            if ACTOR <= address < ACTOR + 0x320:
                out[0] = struct.unpack_from('<I', bytes(actor.bytes), address - ACTOR)[0]
                return 0
            word = nram.word(address, base)
            if word is None:
                return -1
            out[0] = word
            return 0

        w = FootWorkers(None, ACTOR_FN(n_eval), SELECT_FN(n_select), FRAMES_FN(n_frames),
                        REQUEST_FN(n_request), SQRT_FN(lib.em_anim_rest_sqrt_0011E748), READ(n_read))
        w.context = None
        # The sqrt worker's context is the SDK context: a trampoline.
        sqrt_direct = lib.em_anim_rest_sqrt_0011E748

        def n_sqrt(_, x, out):
            return sqrt_direct(C.addressof(sdk), x, out)
        w.sqrt = SQRT_FN(n_sqrt)
        buffers = {name: (C.c_uint32 * (len(v) // 4))(*([0x5A5A5A5A] * (len(v) // 4)) if name in ('s3A20', 's3A24') else [0] * (len(v) // 4))
                   for name, v in want_spad.items()}
        s = FootScratch(*[C.cast(buffers[name], C.POINTER(C.c_uint32))
                          for name in ('s3A20', 's3A24', 's36A0', 's38A0', 's38B0')])
        d275B40 = C.c_uint32(ACTOR + 0x110)
        status = lib.em_player_foot_stop_0017B910(C.byref(w), C.byref(s), C.byref(d275B40), C.byref(actor))
        assert status == 0, ('0017B910 native status', c)
        got_rec = bytes(actor.bytes)
        assert got_rec == want_rec, ('0017B910 record', c,
                                     [hex(i) for i in range(0x320) if got_rec[i] != want_rec[i]])
        for name, want in want_spad.items():
            got = bytes(buffers[name])
            assert got == want, ('0017B910 scratch', name, got.hex(), want.hex(), c)
        assert native_log == log, ('0017B910 calls', native_log, log)
        calls_total += len(log)
    return calls_total


def check_fail_stop(lib):
    """A missing worker or view: -1 before any call or write."""
    checks = 0
    base_w = FootWorkers(None, ACTOR_FN(lambda *_: 0), SELECT_FN(lambda *_: 0), FRAMES_FN(lambda *_: 0),
                         REQUEST_FN(lambda *_: 0), SQRT_FN(lambda *_: 0), READ(lambda *_: 0))
    words = (C.c_uint32 * 64)()
    ptr = C.cast(words, C.POINTER(C.c_uint32))
    base_s = FootScratch(ptr, ptr, ptr, ptr, ptr)
    d = C.c_uint32(ACTOR + 0x110)
    for field in ('eval_skeleton', 'select', 'clip_frames', 'request', 'sqrt', 'read'):
        w = FootWorkers.from_buffer_copy(base_w)
        setattr(w, field, type(getattr(w, field))())
        a = Actor()
        assert lib.em_player_foot_stop_0017B910(C.byref(w), C.byref(base_s), C.byref(d), C.byref(a)) == -1
        assert bytes(a.bytes) == bytes(0x320)
        checks += 1
    for field in ('s3A20', 's3A24', 's36A0', 's38A0', 's38B0'):
        s = FootScratch.from_buffer_copy(base_s)
        setattr(s, field, C.POINTER(C.c_uint32)())
        a = Actor()
        assert lib.em_player_foot_stop_0017B910(C.byref(base_w), C.byref(s), C.byref(d), C.byref(a)) == -1
        checks += 1
    reader = span_reader(bytes(0x2000000))
    rec = (C.c_uint8 * 0x320)()
    rec[0x1F0] = 1
    rec[0x1F1] = 1
    assert lib.em_player_motor_0017BC40(rec, reader, None, None) == -1
    checks += 1
    return checks


def main():
    global ELF_BYTES, SDK_TABLES
    ELF_BYTES = SR.read_elf()
    lib = build()
    SDK_TABLES = (C.c_uint64 * 1024)()
    lib.em_sdk_math_original_load_tables.argtypes = [C.c_char_p, C.c_size_t, C.c_void_p]
    assert lib.em_sdk_math_original_load_tables(ELF_BYTES, len(ELF_BYTES), SDK_TABLES) == 0
    base = bytes(FloatEE(ELF_BYTES).mem)
    for at, size in ((ACTOR, 0x400), (NODE17, 0x100), (NODE18, 0x100)):
        assert base[at:at + size] == bytes(size), ('unit scratch RAM not zero', hex(at))
    rng = random.Random(0x17BC40)
    motor_all = [motor_case(rng, i) for i in range(4000)]
    grid = motor_grid()
    motor = grid + motor_all[:400] + reference_mode.select(motor_all[400:], 200, 0xBC40)
    if reference_mode.FULL:
        motor = grid + motor_all
    motor_all = grid + motor_all
    motor_faults = check_motor(lib, base, motor)
    rng = random.Random(0x17B910)
    foot_all = [foot_case(rng) for _ in range(3000)]
    foot = reference_mode.select(foot_all, 250, 0xB910)
    if reference_mode.FULL:
        foot = foot_all
    calls = check_foot(lib, base, foot)
    fail_stop = check_fail_stop(lib)
    reference_mode.banner(reference_mode.part(len(motor), len(motor_all), '0017BC40 cases'),
                          reference_mode.part(len(foot), len(foot_all), '0017B910 cases'),
                          '%d fail-stop checks' % fail_stop)
    print('player loco workers reference PASS: record bytes and scratch exact; %d 0017B910 worker '
          'calls compared; %d 0017BC40 cases outside the exported span faulted with nothing written'
          % (calls, motor_faults))
    return 0


if __name__ == '__main__':
    sys.exit(main())
