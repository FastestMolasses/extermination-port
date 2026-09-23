#!/usr/bin/env python3
"""Execute the original reversal-skid instructions and compare the native path.

WP-15/H11. The user's pinned ELF supplies every instruction and table; none
are embedded here. The bounded interpreter (tools/test_point_light_reference
Oracle, extended below for the extra EE opcodes these routines use) runs:

  00174AC0  heading/gait latch with the 3pi/4 reversal gate
  0017C030  cases 7 and 6 (and 0017B490/0017B460/001B0070 clip lookup)
  0017BC40  scalar motor (mode 6 decay)
  001612D0  walk callback case 1 (detection tick) and case 2 (skid/resume)

Hooked boundaries (recorded, not simulated): SDK cos/atan2 (host models, only
used to form the desired heading, which is then handed to the native side
unchanged), 001B12B0 turn, 001749A0 / 001749F0 clip requests, 001C61D0 clip
length (read from the original bank header), 001FB9F0 sound, 001EFD90
effect, 00178B90 translation, 0017B660/0017B910/0017C440 and the common
callback tail. The interpreter extension is validated first on 0017BC40,
which em_player_motor.c already matches under test_player_motor_reference.
"""
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_point_light_reference import Oracle, bits, number, signed, fp  # noqa: E402

ELF_SHA256 = 'ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a'
ACTOR = 0x680000
LIBC = C.CDLL(None)
LIBC.cosf.argtypes = [C.c_float]; LIBC.cosf.restype = C.c_float
LIBC.atan2f.argtypes = [C.c_float, C.c_float]; LIBC.atan2f.restype = C.c_float

# Original callees.
HEADING, MOTOR, ANIMATION, WALK = 0x174AC0, 0x17BC40, 0x17C030, 0x1612D0
WRAP, TURN = 0x1B1470, 0x1B12B0
REQUEST, ARBITER, LENGTH = 0x1749A0, 0x1749F0, 0x1C61D0
SOUND, EFFECT, TRANSLATE = 0x1FB9F0, 0x1EFD90, 0x178B90
GAIT_MATRIX, FOOT_SOLVE, REENTRY = 0x17B660, 0x17B910, 0x17C440
ACTION, USE, USE_SCAN = 0x1607D0, 0x160220, 0x184BA0
TAIL = (0x1764E0, 0x175900, 0x1756E0, 0x1796C0)

FIELDS = {  # native field -> (actor offset, size, float)
    'speed': (0x38, 4, True), 'yaw': (0xC4, 4, True), 'target': (0x240, 4, True),
    'rate': (0x204, 4, True), 'blend': (0x208, 4, True),
    'anim_flags': (0x200, 4, False), 'ticks': (0x28, 2, False),
    'player_state': (5, 1, False), 'walk_state': (6, 1, False),
    'mode': (0x1F0, 1, False), 'variant': (0x1F1, 1, False), 'tier': (0x25C, 1, False),
    'gait': (0x23F, 1, False), 'row': (0x235, 1, False), 'special': (0x236, 1, False),
    'surface': (0x23A, 1, False), 'obstruction': (0x314, 1, False),
}


class Actor(C.Structure):
    _fields_ = [('speed', C.c_float), ('yaw', C.c_float), ('target', C.c_float),
                ('rate', C.c_float), ('blend', C.c_float), ('delta', C.c_float),
                ('anim_flags', C.c_uint32), ('global_mode', C.c_uint32),
                ('ticks', C.c_uint16), ('player_state', C.c_uint8), ('walk_state', C.c_uint8),
                ('mode', C.c_uint8), ('variant', C.c_uint8), ('tier', C.c_uint8),
                ('gait', C.c_uint8), ('row', C.c_uint8), ('special', C.c_uint8),
                ('surface', C.c_uint8), ('depth', C.c_uint8 * 2), ('obstruction', C.c_uint8)]


class Motor(C.Structure):
    _fields_ = [('speed', C.c_float), ('target', C.c_float), ('rate', C.c_float), ('blend', C.c_float),
                ('mode', C.c_uint8), ('substate', C.c_uint8), ('tier', C.c_uint8),
                ('gait', C.c_uint8), ('obstruction', C.c_uint8)]


REQUEST_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int, C.c_float)
ARBITER_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_float, C.c_float)
FRAMES_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.POINTER(C.c_int))
SOUND_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint)
EFFECT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32)
TURN_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_float)


class Workers(C.Structure):
    _fields_ = [('context', C.c_void_p), ('request', REQUEST_FN), ('arbiter', ARBITER_FN),
                ('clip_frames', FRAMES_FN), ('sound', SOUND_FN), ('effect', EFFECT_FN),
                ('turn', TURN_FN)]


class Reversal(Oracle):
    """Oracle plus the EE opcodes the player callbacks use."""

    def __init__(self, elf, bank):
        super().__init__(elf)
        for i in range(4): self.mem.pop(0x275670 + i, None)  # point-light context
        # 0017BC40's blend division truncates, as in test_player_motor_reference
        # (and em_player_motor.c); the other divisions here are exact.
        self.truncate_ee_division = True
        self.bank = bank
        self.log = []
        self.calls.update({
            0x11DE90: lambda o: o.ret_float(LIBC.cosf(number(o.f[12]))),
            0x11E620: lambda o: o.ret_float(LIBC.atan2f(number(o.f[12]), number(o.f[13]))),
            TURN: lambda o: (o.log.append(('turn', o.f[12])), o.ret_float(number(o.f[13]))),
            REQUEST: lambda o: o.record('request', o.r[5] & 0xffff, o.r[6], o.f[12]),
            ARBITER: lambda o: o.record('arbiter', o.r[5] & 0xffff, o.f[12], o.f[13]),
            LENGTH: lambda o: o.ret_int(self.clip_length(signed(o.r[5], 16))),
            SOUND: lambda o: o.record('sound', o.r[4], o.r[5], o.r[6], o.r[7]),
            EFFECT: lambda o: o.record('effect', o.r[4], o.r[5] - ACTOR, o.r[6] - ACTOR),
            TRANSLATE: lambda o: o.record('translate', o.r[5], o.load(ACTOR + 0x38)),
            GAIT_MATRIX: lambda o: o.record('gait_matrix'),
            FOOT_SOLVE: lambda o: o.record('foot_solve'),
            REENTRY: lambda o: o.record('reentry'),
            ACTION: lambda o: o.ret_int(0),
            USE: lambda o: o.ret_int(0),
            USE_SCAN: lambda o: o.record('use_scan'),
        })
        for address in TAIL: self.calls[address] = lambda o: None

    def clip_length(self, clip):
        header = struct.unpack_from('<I', self.bank, 4 + clip * 4)[0]
        return struct.unpack_from('<H', self.bank, header + 2)[0]

    def ret_float(self, value): self.f[0] = bits(value)
    def ret_int(self, value): self.r[2] = value & 0xffffffff
    def record(self, *entry): self.log.append(entry); self.r[2] = 0

    def plain(self, word):
        r, f = self.r, self.f
        op, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
        imm = signed(word & 65535, 16)
        if op == 0:
            fn, shift = word & 63, word >> 6 & 31
            if fn == 60: r[rd] = (r[rt] << (shift + 32)) & 0xffffffffffffffff; return
            if fn == 63:
                value = r[rt] & 0xffffffffffffffff
                value = value - (1 << 64) if value >> 63 else value
                r[rd] = (value >> (shift + 32)) & 0xffffffff
                return
            if fn == 43: r[rd] = int((r[rs] & 0xffffffff) < (r[rt] & 0xffffffff)); return
            if fn == 38: r[rd] = (r[rs] ^ r[rt]) & 0xffffffff; return
            if fn == 39: r[rd] = ~(r[rs] | r[rt]) & 0xffffffff; return
            if fn == 0 and rd == 0: return  # nop
        elif op == 11: r[rt] = int((r[rs] & 0xffffffff) < (imm & 0xffffffff)); r[0] = 0; return
        elif op == 14: r[rt] = r[rs] ^ (word & 65535); r[0] = 0; return
        elif op in (32, 37):
            address = (r[rs] + imm) & 0xffffffff
            value = self.load(address, 1 if op == 32 else 2)
            r[rt] = (signed(value, 8) & 0xffffffff) if op == 32 else value
            r[0] = 0
            return
        elif op == 17 and rs == 16 and (word & 63) in (5, 7):
            x = number(f[rd])
            f[word >> 6 & 31] = bits(abs(x)) if (word & 63) == 5 else bits(-x)
            return
        super().plain(word)

    def put(self, field, value):
        offset, size, is_float = FIELDS[field]
        self.save(ACTOR + offset, bits(value) if is_float else value, size)

    def get(self, field):
        offset, size, is_float = FIELDS[field]
        value = self.load(ACTOR + offset, size)
        return number(value) if is_float else value

    def call(self, entry, *args):
        self.r[29] = 0x700000
        self.run(entry, args)


def load_actor(oracle, actor, depth, global_mode, pad, camera):
    for name in FIELDS:
        oracle.put(name, getattr(actor, name))
    oracle.save(ACTOR + 0x23C, depth[0], 1); oracle.save(ACTOR + 0x23D, depth[1], 1)
    oracle.save(0x8106C8, global_mode)
    oracle.save(0x810E57, actor.gait, 1)
    oracle.save(0x810E64, pad[0], 1); oracle.save(0x810E65, pad[1], 1)
    oracle.save(0x8106A0, bits(camera))
    oracle.save(ACTOR + 0x40, 0x500000)


def compare(oracle, actor, fields, label):
    for name in fields:
        offset, size, is_float = FIELDS[name]
        expected = oracle.load(ACTOR + offset, size)
        actual = getattr(actor, name)
        actual = bits(actual) if is_float else actual
        assert actual == expected, (label, name, hex(actual), hex(expected))


class NativeLog:
    def __init__(self, bank):
        self.bank, self.entries = bank, []
        self.workers = Workers(None, REQUEST_FN(self.request), ARBITER_FN(self.arbiter),
                               FRAMES_FN(self.frames), SOUND_FN(self.sound),
                               EFFECT_FN(self.effect), TURN_FN(self.turn))

    def request(self, _, clip, force, blend):
        self.entries.append(('request', clip & 0xffff, force, bits(blend))); return 0

    def arbiter(self, _, clip, blend, frame):
        self.entries.append(('arbiter', clip & 0xffff, bits(blend), bits(frame))); return 0

    def frames(self, _, clip, out):
        header = struct.unpack_from('<I', self.bank, 4 + clip * 4)[0]
        out[0] = struct.unpack_from('<H', self.bank, header + 2)[0]; return 0

    def sound(self, _, sound):
        self.entries.append(('sound', sound, 0x1000, 0x1000, 0x1000)); return 0

    def effect(self, _, effect):
        self.entries.append(('effect', effect, 0xB0, 0xC0)); return 0

    def turn(self, _, desired):
        self.entries.append(('turn', bits(desired))); return 0


def filtered(log, kinds):
    return [entry for entry in log if entry[0] in kinds]


def desired_heading(elf, bank, actor, depth, global_mode, pad, camera):
    """The desired heading 00174AC0 forms (arg1==2 records it at +218)."""
    probe = Reversal(elf, bank)
    load_actor(probe, actor, depth, global_mode, pad, camera)
    probe.put('player_state', 0)  # no gate side effects on the probe copy
    probe.call(HEADING, ACTOR, 2)
    return number(probe.load(ACTOR + 0x218))


def random_actor(rng, **forced):
    actor = Actor()
    actor.speed = rng.choice([0.0, 0.3, 0.5, number(bits(0.5) + 1), 0.55, 0.75, 0.8, rng.uniform(0, 1)])
    actor.yaw = rng.choice([rng.uniform(-math.pi, math.pi), number(bits(math.pi)), -number(bits(math.pi))])
    actor.target = rng.choice([0.0, 0.1, 0.3, 0.8])
    actor.rate = rng.choice([1.0, 0.75, 1.5])
    actor.blend = rng.choice([0.0, 0.5, 1.0])
    actor.anim_flags = rng.choice([0, 0x1000, 0x8000, 0x9000, 0x1001])
    actor.ticks = rng.randrange(40)
    actor.player_state = rng.choice([1, 1, 1, 0, 2])
    actor.walk_state = rng.choice([1, 2])
    actor.mode = rng.randrange(8)
    actor.variant = rng.choice([3, 4, 1, 0, 2])
    actor.tier = rng.randrange(4)
    actor.gait = rng.randrange(4)
    actor.row = rng.randrange(4)
    actor.special = rng.choice([0, 0, 1])
    actor.surface = rng.choice([0, 1, 5, 6, 7, 0x5A])
    actor.obstruction = rng.randrange(256)
    for name, value in forced.items(): setattr(actor, name, value)
    return actor


def main():
    decomp = ROOT.parent / 'Extermination'
    elf = (decomp / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'wrong original executable'
    bank = (decomp / 'extract/chunk28/f01_id3c.bin').read_bytes()
    out = ROOT / 'build/player_reversal_reference'; out.mkdir(parents=True, exist_ok=True)
    lib = out / ('reversal.dylib' if sys.platform == 'darwin' else 'reversal.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_player_reversal.c',
                    'src/game/em_player_motor.c', '-lm', '-o', str(lib)], cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    native.em_player_reversal_wrap.argtypes = [C.c_float]; native.em_player_reversal_wrap.restype = C.c_float
    native.em_player_reversal_heading.argtypes = [C.POINTER(Actor), C.c_float]
    native.em_player_reversal_animation.argtypes = [C.POINTER(Actor), C.POINTER(Workers)]
    native.em_player_reversal_walk_tail.argtypes = [C.POINTER(Actor)]
    native.em_player_reversal_state2.argtypes = [C.POINTER(Actor), C.c_float, C.POINTER(Workers)]
    native.em_player_reversal_clip.argtypes = [C.POINTER(Actor), C.c_uint, C.POINTER(C.c_int)]
    native.em_player_motor_tick.argtypes = [C.POINTER(Motor)]
    rng = random.Random(0x174AC0)
    result = {}

    # 0. Interpreter extension check on already-verified code: 0017BC40
    # (em_player_motor.c matches it under test_player_motor_reference) and
    # the byte-matched 001B1470 wrap.
    checks = 0
    for _ in range(3000):
        m = Motor(); m.mode = rng.choice([1, 2, 6, 0, 3]); m.substate = rng.randrange(3); m.tier = rng.randrange(4)
        if (m.mode == 1 and ((m.substate == 1 and m.tier == 3) or (m.substate == 2 and m.tier == 0))) or (m.mode == 2 and m.tier == 0):
            continue
        m.gait = rng.randrange(4); m.target = (0, .1, .3, .8)[m.gait]
        m.speed = rng.choice([0, .05, .1, .3, .5, .8, rng.random()]); m.rate = 1; m.blend = rng.choice([0, .5])
        m.obstruction = rng.randrange(256)
        oracle = Reversal(elf, bank)
        for name, (offset, size) in {'speed': (0x38, 4), 'target': (0x240, 4), 'rate': (0x204, 4), 'blend': (0x208, 4),
                                     'mode': (0x1F0, 1), 'substate': (0x1F1, 1), 'tier': (0x25C, 1),
                                     'gait': (0x23F, 1), 'obstruction': (0x314, 1)}.items():
            value = getattr(m, name); oracle.save(ACTOR + offset, bits(value) if size == 4 else value, size)
        oracle.call(MOTOR, ACTOR)
        native.em_player_motor_tick(C.byref(m))
        assert bits(m.speed) == oracle.load(ACTOR + 0x38) and m.mode == oracle.load(ACTOR + 0x1F0, 1)
        assert m.tier == oracle.load(ACTOR + 0x25C, 1) and bits(m.rate) == oracle.load(ACTOR + 0x204)
        checks += 1
    for _ in range(3000):
        angle = rng.choice([rng.uniform(-20, 20), number(bits(math.pi)), -number(bits(math.pi)),
                            number(bits(math.pi) + 1), -number(bits(math.pi) + 1), 0.0])
        oracle = Reversal(elf, bank); oracle.f[12] = bits(angle); oracle.call(WRAP)
        assert bits(native.em_player_reversal_wrap(angle)) == oracle.f[0], angle
        checks += 1
    result['interpreter_validation_cases'] = checks

    # 1. 00174AC0 reversal gate (full routine, arg1 == 1).
    gate_cases = triggers = 0
    for case in range(4000):
        actor = random_actor(rng)
        if case % 3 == 0:  # an eligible walking callback, aimed at the boundary below
            actor.player_state = 1; actor.mode = rng.randrange(6)
            actor.speed = rng.choice([number(bits(0.5) + 1), 0.8, 0.55]); actor.gait = rng.choice([2, 3])
        pad = (rng.randrange(256), rng.randrange(256)); camera = rng.uniform(-math.pi, math.pi)
        depth = (0, 0)
        desired = desired_heading(elf, bank, actor, depth, 0, pad, camera)
        if case % 3 == 0:  # aim at the 3pi/4 boundary on either side
            limit = number(bits(2.3561945)); step = rng.randrange(-3, 4)
            edge = number(bits(limit) + step) * rng.choice([1, -1])
            actor.yaw = number(bits(fp(desired - edge)))
        oracle = Reversal(elf, bank); load_actor(oracle, actor, depth, 0, pad, camera)
        oracle.call(HEADING, ACTOR, 1)
        log = NativeLog(bank); before = actor.mode
        turn = native.em_player_reversal_heading(C.byref(actor), desired)
        if turn: log.turn(None, desired)
        compare(oracle, actor, ('mode', 'variant', 'target', 'yaw'), ('gate', case))
        assert filtered(oracle.log, ('turn',)) == log.entries, ('turn', case)
        if before != 7 and actor.mode == 7:
            assert bits(actor.delta) == oracle.load(0x70003A20), ('delta', case)
            triggers += 1
        gate_cases += 1
    assert triggers > 300, triggers
    result['heading_gate_cases'] = gate_cases; result['heading_gate_new_skids'] = triggers

    # 2. 0017C030 cases 7 and 6 (and the not-handled modes).
    animation_cases = 0
    for case in range(4000):
        actor = random_actor(rng, mode=rng.choice([6, 7, 6, 7, 0, 1, 2]))
        global_mode = rng.choice([0x20081910, 0x20081914, 0, 4])
        oracle = Reversal(elf, bank); load_actor(oracle, actor, (0, 0), global_mode, (128, 128), 0.0)
        before_mode = actor.mode
        actor.global_mode = global_mode
        oracle.call(ANIMATION, ACTOR)
        log = NativeLog(bank)
        handled = native.em_player_reversal_animation(C.byref(actor), C.byref(log.workers))
        assert handled == (1 if before_mode in (6, 7) else 0), (case, handled)
        if handled:
            compare(oracle, actor, ('mode', 'tier', 'speed', 'yaw', 'rate', 'variant'), ('animation', case))
            assert filtered(oracle.log, ('request', 'sound')) == log.entries, (case, oracle.log, log.entries)
        animation_cases += 1
    result['animation_cases'] = animation_cases

    # 3. 001612D0 case 2 (skid ticks, resume and idle return).
    state2_cases = 0; kinds = ('request', 'arbiter', 'sound', 'effect', 'turn')
    for case in range(4000):
        actor = random_actor(rng, player_state=1, walk_state=2, mode=rng.choice([6, 6, 7, 0, 0]))
        depth = (rng.choice([0, 0, 1]), rng.choice([0, 0, 2]))
        actor.depth[0], actor.depth[1] = depth
        global_mode = rng.choice([0x20081910, 0x20081914])
        pad = (rng.randrange(256), rng.randrange(256)); camera = rng.uniform(-math.pi, math.pi)
        desired = desired_heading(elf, bank, actor, depth, global_mode, pad, camera)
        oracle = Reversal(elf, bank); load_actor(oracle, actor, depth, global_mode, pad, camera)
        actor.global_mode = global_mode
        oracle.call(WALK, ACTOR)
        log = NativeLog(bank)
        assert native.em_player_reversal_state2(C.byref(actor), desired, C.byref(log.workers)) == 1
        compare(oracle, actor, ('player_state', 'walk_state', 'mode', 'variant', 'tier', 'speed', 'yaw',
                                'rate', 'blend', 'ticks', 'target'), ('state2', case))
        assert filtered(oracle.log, kinds) == log.entries, (case, oracle.log, log.entries)
        translations = filtered(oracle.log, ('translate',))
        assert translations == [('translate', 0, bits(actor.speed))], (case, translations)
        state2_cases += 1
    result['walk_case2_cases'] = state2_cases

    # 4. 001612D0 case 1: the detection tick hands over to state 2.
    detections = case1 = 0
    for case in range(3000):
        actor = random_actor(rng, player_state=1, walk_state=1, mode=1, gait=rng.choice([2, 3, 3, 1]),
                             speed=rng.choice([0.8, 0.8, 0.55, number(bits(0.5) + 1), 0.3]),
                             variant=rng.choice([0, 1, 2]), tier=rng.choice([2, 3]))
        pad = (rng.randrange(256), rng.randrange(256)); camera = rng.uniform(-math.pi, math.pi)
        desired = desired_heading(elf, bank, actor, (0, 0), 0x20081910, pad, camera)
        if rng.randrange(2):
            actor.yaw = number(bits(fp(desired - rng.choice([1, -1]) * rng.uniform(2.3, math.pi))))
        oracle = Reversal(elf, bank); load_actor(oracle, actor, (0, 0), 0x20081910, pad, camera)
        actor.global_mode = 0x20081910
        oracle.call(WALK, ACTOR)
        log = NativeLog(bank)
        turn = native.em_player_reversal_heading(C.byref(actor), desired)
        if turn: log.turn(None, desired)
        m = Motor(actor.speed, actor.target, actor.rate, actor.blend, actor.mode, actor.variant,
                  actor.tier, actor.gait, actor.obstruction)
        native.em_player_motor_tick(C.byref(m))
        if actor.mode == 7:
            assert m.mode == 7 and bits(m.speed) == bits(actor.speed)  # 0017BC40 mode 7 is inert
            assert native.em_player_reversal_animation(C.byref(actor), C.byref(log.workers)) == 1
            assert native.em_player_reversal_walk_tail(C.byref(actor)) == 1
            compare(oracle, actor, ('walk_state', 'ticks', 'mode', 'variant', 'speed', 'yaw', 'tier'), ('case1', case))
            assert filtered(oracle.log, ('request', 'sound', 'turn')) == log.entries, (case, oracle.log)
            assert filtered(oracle.log, ('translate',)) == [('translate', 0, bits(actor.speed))]
            detections += 1
        else:
            assert oracle.load(ACTOR + 0x1F0, 1) not in (6, 7), case
            assert not filtered(oracle.log, ('sound',)), case
        case1 += 1
    assert detections > 500, detections
    result['walk_case1_cases'] = case1; result['walk_case1_detections'] = detections

    # 5. Whole sequences: run speed, stick reversed, skid, end flag, then a
    # resume (stick held) or an idle return (stick released).
    sequences = 0; outcomes = {'resume_request': 0, 'resume_arbiter': 0, 'idle_return': 0, 'effects': 0}
    for sequence in range(160):
        variant_side = rng.choice([1, -1]); end_tick = rng.randrange(2, 24)
        release = rng.randrange(2); gait = rng.choice([2, 3])
        global_mode = rng.choice([0x20081910, 0x20081914]); row = rng.choice([0, 0, 1, 2])
        actor = Actor(); actor.speed = 0.8; actor.target = 0.8; actor.rate = 1.0; actor.blend = 0.0
        actor.player_state = 1; actor.walk_state = 1; actor.mode = 1; actor.variant = 0; actor.tier = 3
        actor.gait = gait; actor.row = row; actor.surface = rng.choice([5, 0, 6]); actor.global_mode = global_mode
        pad = (128, 0); camera = rng.uniform(-math.pi, math.pi)
        desired = desired_heading(elf, bank, actor, (0, 0), global_mode, pad, camera)
        actor.yaw = number(bits(fp(desired - variant_side * rng.uniform(2.4, 3.1))))
        oracle = Reversal(elf, bank); load_actor(oracle, actor, (0, 0), global_mode, pad, camera)
        trace = []
        for tick in range(40):
            if actor.player_state == 0:
                break  # 00161020 owns the next callback
            flags = 0x1000 if tick >= end_tick else 0
            oracle.save(ACTOR + 0x200, flags); actor.anim_flags = flags
            actor.gait = 0 if release and tick > end_tick else gait
            pad = (128, 0) if actor.gait else (128, 128)
            oracle.save(0x810E57, actor.gait, 1)
            oracle.save(0x810E64, pad[0], 1); oracle.save(0x810E65, pad[1], 1)
            desired = desired_heading(elf, bank, actor, (0, 0), global_mode, pad, camera) if actor.gait else 0.0
            oracle.log = []; state = actor.walk_state
            oracle.call(WALK, ACTOR)
            log = NativeLog(bank)
            if state == 2:
                assert native.em_player_reversal_state2(C.byref(actor), desired, C.byref(log.workers)) == 1
            else:
                turn = native.em_player_reversal_heading(C.byref(actor), desired)
                if turn: log.turn(None, desired)
                m = Motor(actor.speed, actor.target, actor.rate, actor.blend, actor.mode, actor.variant,
                          actor.tier, actor.gait, actor.obstruction)
                native.em_player_motor_tick(C.byref(m))
                actor.speed, actor.rate, actor.blend = m.speed, m.rate, m.blend
                actor.mode, actor.variant, actor.tier = m.mode, m.substate, m.tier
                native.em_player_reversal_animation(C.byref(actor), C.byref(log.workers))
                native.em_player_reversal_walk_tail(C.byref(actor))
            compare(oracle, actor, ('player_state', 'walk_state', 'mode', 'variant', 'tier', 'speed', 'yaw',
                                    'ticks', 'target'), ('sequence', sequence, tick))
            skid = filtered(oracle.log, ('request', 'arbiter', 'sound', 'effect'))
            assert skid == filtered(log.entries, ('request', 'arbiter', 'sound', 'effect')), \
                (sequence, tick, skid, log.entries)
            trace.append((actor.walk_state, actor.mode, round(actor.speed, 4)))
            if state == 2 and actor.walk_state == 1:
                outcomes['resume_' + ('request' if filtered(log.entries, ('request',)) else 'arbiter')] += 1
            if state == 2 and actor.player_state == 0:
                outcomes['idle_return'] += 1
            if filtered(log.entries, ('effect',)):
                outcomes['effects'] += 1
            # Leave once the callback is ordinary walking again.
            if state == 2 and actor.walk_state == 1:
                break
        assert any(step[1] == 6 for step in trace), trace
        sequences += 1
    assert all(outcomes.values()), outcomes
    result['sequences'] = sequences; result['sequence_outcomes'] = outcomes

    # 6. A reached missing worker is a fault (-1), never a silent skip.
    faults = 0
    def workers_without(name):
        log = NativeLog(bank); workers = log.workers
        setattr(workers, name, type(getattr(workers, name))())  # NULL
        return log, workers
    for name, mode, flags in (('request', 7, 0), ('sound', 7, 0), ('request', 6, 0x1000)):
        actor = random_actor(rng, mode=mode, anim_flags=flags, row=0)
        log, workers = workers_without(name)
        before = (actor.mode, bits(actor.yaw), bits(actor.speed))
        assert native.em_player_reversal_animation(C.byref(actor), C.byref(workers)) == -1, name
        if name == 'request': assert (actor.mode, bits(actor.yaw), bits(actor.speed)) == before
        faults += 1
    for name, fields in (('effect', dict(mode=6, ticks=0, surface=5)),
                         ('turn', dict(mode=0, gait=3, speed=0.0)),
                         ('arbiter', dict(mode=0, gait=2, variant=4, speed=0.0, row=0)),
                         ('clip_frames', dict(mode=0, gait=2, variant=4, speed=0.0, row=0))):
        actor = random_actor(rng, player_state=1, walk_state=2, **fields)
        log, workers = workers_without(name)
        assert native.em_player_reversal_state2(C.byref(actor), 0.0, C.byref(workers)) == -1, name
        faults += 1
    actor = random_actor(rng, row=3, special=0, mode=7); actor.global_mode = 0
    clip = C.c_int(-1)
    assert native.em_player_reversal_clip(C.byref(actor), 2, C.byref(clip)) == 1 and clip.value == 0x51
    actor.row = 5
    assert native.em_player_reversal_clip(C.byref(actor), 2, C.byref(clip)) == 0  # unverified row
    log = NativeLog(bank)
    assert native.em_player_reversal_animation(C.byref(actor), C.byref(log.workers)) == -1 and not log.entries
    result['fault_cases'] = faults + 1

    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print('player reversal original-instruction PASS', json.dumps(result))


if __name__ == '__main__':
    main()
