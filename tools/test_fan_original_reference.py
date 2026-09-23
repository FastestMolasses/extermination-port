#!/usr/bin/env python3
"""Compare the native AREA11 fan (em_fan_original.c) with the original code.

The oracle executes the ORIGINAL instructions of the AREA11 overlay behaviour
0x827630 (loaded from the user's extract/OVERLAY/AREA11.BIN at its arena
0x823500) and, from the user's pinned boot ELF, 001B0FD0, 001B0EA0 and
001B1470. Leaf callees (model/bone allocation, 001C6380 matrix build, 001B17A0
publication, 001FBD50 sound, 001B0C60 area-change request, the +0x4C draw
callback and the 001AFC10 free) are recorded as calls. Every memory byte the
original writes is checked to be one the native module models (or one owned
by a worker), and every modelled byte is compared.

Arithmetic: add.s/sub.s use the single-guard-bit model of
test_pose_transition_reference. The capture check below shows that this
model, and neither plain truncation nor round-to-nearest, reproduces the fan
states captured in every build/startup-reference RAM image.

No original instruction bytes, disassembly or data are written by this file;
the report in build/ holds only counts.
"""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess

from test_door_original_reference import DoorOracle
from test_interaction_scan_reference import DECOMP, ELF_SHA
from test_pickup_owner_reference import ACTOR, DRAW
from test_point_light_reference import STACK, bits, number
from test_pose_transition_reference import add as ee_add

ROOT = Path(__file__).resolve().parents[1]
ENTRY, ARENA, OVERLAY_SIZE = 0x827630, 0x823500, 0x7800
CODE = (0x4130, 0x4610)            # file range of the 0x827630 body
PLAYER = 0x8102B0
G788, GB8, G758, G7D8 = 0x810788, 0x8106B8, 0x810758, 0x8107D8
BONE_CAP = 0x275BCC                # D_00275BCC (lh, gp-relative in 001B0EA0)
SLOTS = 0x960000
CAPTURES = ['opening_ee.bin', 'handoff_ee.bin', 'playable_ee.bin',
            'elevator/completed_ee.bin', 'elevator/clip47_ee.bin',
            'elevator/refusal/eeMemory.bin', 'status-hub/eeMemory.bin',
            'roger-encounter/eeMemory.bin', 'panel/animation_ee.bin',
            'panel/eeMemory.bin', 'panel/root/eeMemory.bin']
POOL, RECORD = 0x7A5640, 0x2F0


class FanOracle(DoorOracle):
    def plain(self, word):
        op, rs, fn = word >> 26, word >> 21 & 31, word & 63
        if op == 17 and rs == 16 and fn in (0, 1):
            a, b = number(self.f[word >> 11 & 31]), number(self.f[word >> 16 & 31])
            self.f[word >> 6 & 31] = bits(ee_add(a, b if fn == 0 else -b))
            return
        super().plain(word)


class Fan(C.Structure):
    _fields_ = [('lifecycle', C.c_uint8), ('phase', C.c_uint8), ('timer', C.c_int16),
                ('flags2', C.c_uint16), ('spin', C.c_float), ('rot_z', C.c_float),
                ('freed', C.c_uint8)]


class Player(C.Structure):
    _fields_ = [('b00', C.c_uint8), ('b0F', C.c_uint8), ('f70', C.c_float * 4),
                ('pos', C.c_float * 3), ('f224', C.c_float)]


class Globals(C.Structure):
    _fields_ = [(name, C.c_uint8) for name in ('d810788', 'd8106B8', 'd810758', 'd8107D8')]


class Fault(C.Structure):
    _fields_ = [('address', C.c_uint32), ('code', C.c_int32)]


FANW = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Fan))
SOUND = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, C.c_int32, C.c_float)
CALL = C.CFUNCTYPE(C.c_int, C.c_void_p)
EXIT = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int32, C.c_int32, C.c_int32)


class Workers(C.Structure):
    _fields_ = [('ctx', C.c_void_p), ('w_001B0FD0', FANW), ('w_001FBD50', SOUND),
                ('w_001C6380', FANW), ('w_001B17A0', CALL), ('w_001B0C60', EXIT),
                ('w_draw_4C', CALL), ('w_001AFC10', CALL)]


def f32(value): return number(bits(value))
def up(value): return float(struct.unpack('<f', struct.pack('<I', bits(value) + (1 if value >= 0 else -1)))[0]) if value else 1e-45
def down(value): return float(struct.unpack('<f', struct.pack('<I', bits(value) - (1 if value > 0 else -1)))[0])


FIELDS = {'lifecycle': (4, 1), 'phase': (5, 1), 'timer': (0x28, 2), 'flags2': (0x2E, 2),
          'spin': (0x38, 4), 'rot_z': (0xC8, 4)}
PLAYER_BYTES = {PLAYER + 0: 1, PLAYER + 0x0F: 1, PLAYER + 0x224: 4,
                **{PLAYER + 0x70 + 4*i: 4 for i in range(4)},
                **{PLAYER + 0xA0 + 4*i: 4 for i in range(3)}}


class Case:
    """One original state and its native twin; tick() runs both once."""

    def __init__(self, elf, overlay, lib, fan, player, glob, bones=3, cap=0x40,
                 sound=0, publish=1):
        self.lib, self.bones, self.cap = lib, bones, cap
        self.sound_result, self.publish_result = sound, publish
        o = self.o = FanOracle(elf)
        o.write(ENTRY, overlay[CODE[0]:CODE[1]])
        o.save(ACTOR + 0x4C, DRAW)
        o.save(ACTOR + 0xD, 0x13, 1)
        o.save(BONE_CAP, cap, 2)
        self.fan, self.player, self.glob = fan, player, glob
        self.push()
        self.expected, self.actual = [], []
        o.calls.update({
            0x1CA6E0: self.o_init, 0x1C6120: lambda r: r.r.__setitem__(2, 0x970000),
            0x1C6150: lambda r: r.r.__setitem__(2, self.bones),
            0x1AF780: self.o_slot, 0x1CB5B0: lambda r: None,
            0x1C62C0: lambda r: self.expected.append(('bone_init',)),
            0x1FBD50: self.o_sound, 0x1C6380: self.o_matrix, 0x1B17A0: self.o_publish,
            0x1B0C60: lambda r: self.expected.append(('exit', r.r[4], r.r[5], r.r[6])),
            DRAW: self.o_one('draw'), 0x1AFC10: self.o_one('free')})
        self.slot = 0
        self.workers = Workers(None, FANW(self.n_init), SOUND(self.n_sound), FANW(self.n_matrix),
            CALL(self.n_publish), EXIT(lambda _, a, b, c: self.actual.append(('exit', a, b, c)) or 0),
            CALL(lambda _: self.actual.append(('draw',)) or 0),
            CALL(lambda _: self.actual.append(('free',)) or 0))

    # ---- original side
    def o_one(self, name):
        def call(r):
            assert r.r[4] == ACTOR, (name, hex(r.r[4]))
            self.expected.append((name,))
        return call

    def o_init(self, r):
        assert r.r[4] == ACTOR
        self.expected.append(('init',))

    def o_slot(self, r):
        r.r[2] = SLOTS + 0x40 * self.slot; self.slot += 1

    def o_sound(self, r):
        assert r.r[4] == ACTOR
        self.expected.append(('sound', r.r[5], r.r[6], r.f[12]))
        r.r[2] = self.sound_result & 0xffffffff

    def o_matrix(self, r):
        assert r.r[4] == ACTOR
        self.expected.append(('matrix', r.load(ACTOR + 0xC8)))

    def o_publish(self, r):
        assert r.r[4] == ACTOR
        self.expected.append(('publish',)); r.r[2] = self.publish_result

    # ---- native side
    def n_init(self, _, fan):
        self.actual.append(('init',))
        over = int(self.cap < self.bones)
        if not over: self.actual.append(('bone_init',))
        return over

    def n_sound(self, _, cue, a2, f12):
        self.actual.append(('sound', cue, a2, bits(f12))); return 0

    def n_matrix(self, _, fan):
        self.actual.append(('matrix', bits(fan[0].rot_z))); return 0

    def n_publish(self, _):
        self.actual.append(('publish',)); return self.publish_result

    # ---- state transfer
    def push(self):
        o, fan, p, g = self.o, self.fan, self.player, self.glob
        o.save(ACTOR + 4, fan.lifecycle, 1); o.save(ACTOR + 5, fan.phase, 1)
        o.save(ACTOR + 0x28, fan.timer & 0xffff, 2); o.save(ACTOR + 0x2E, fan.flags2, 2)
        o.save(ACTOR + 0x38, bits(fan.spin)); o.save(ACTOR + 0xC8, bits(fan.rot_z))
        o.save(PLAYER, p.b00, 1); o.save(PLAYER + 0x0F, p.b0F, 1); o.save(PLAYER + 0x224, bits(p.f224))
        for i in range(4): o.save(PLAYER + 0x70 + 4*i, bits(p.f70[i]))
        for i in range(3): o.save(PLAYER + 0xA0 + 4*i, bits(p.pos[i]))
        for address, name in ((G788, 'd810788'), (GB8, 'd8106B8'), (G758, 'd810758'), (G7D8, 'd8107D8')):
            o.save(address, getattr(g, name), 1)

    def original_state(self):
        o = self.o
        fan = tuple(o.load(ACTOR + off, size) for off, size in FIELDS.values())
        player = tuple(o.load(a, s) for a, s in sorted(PLAYER_BYTES.items()))
        glob = tuple(o.load(a, 1) for a in (G788, GB8, G758, G7D8))
        return fan, player, glob

    def native_state(self):
        fan, p, g = self.fan, self.player, self.glob
        fs = (fan.lifecycle, fan.phase, fan.timer & 0xffff, fan.flags2, bits(fan.spin), bits(fan.rot_z))
        values = {PLAYER: p.b00, PLAYER + 0x0F: p.b0F, PLAYER + 0x224: bits(p.f224),
                  **{PLAYER + 0x70 + 4*i: bits(p.f70[i]) for i in range(4)},
                  **{PLAYER + 0xA0 + 4*i: bits(p.pos[i]) for i in range(3)}}
        return fs, tuple(values[a] for a in sorted(PLAYER_BYTES)), (g.d810788, g.d8106B8, g.d810758, g.d8107D8)

    def tick(self, label):
        o = self.o
        o.r = [0]*32; o.f = [0]*32; o.r[28], o.r[29] = 0x27d370, STACK
        before = dict(o.mem)
        self.expected.clear(); self.actual.clear(); self.slot = 0
        o.run(ENTRY, (ACTOR,))
        fault = Fault()
        result = self.lib.em_fan_original_tick(C.byref(self.fan), C.byref(self.player),
                                               C.byref(self.glob), C.byref(self.workers), C.byref(fault))
        # Every byte the original changed must be modelled or worker-owned.
        allowed = set()
        for off, size in FIELDS.values(): allowed.update(ACTOR + off + i for i in range(size))
        for a, s in PLAYER_BYTES.items(): allowed.update(a + i for i in range(s))
        allowed.update((G788, GB8, G758, G7D8))
        allowed.update((ACTOR + 9, ACTOR + 0xC))                   # 001B0EA0 (worker)
        allowed.update(ACTOR + 0x110 + i for i in range(4 * 256))  # 001B0EA0 slots (worker)
        changed = {a for a in set(o.mem) | set(before)
                   if o.mem.get(a, 0) != before.get(a, 0) and not STACK - 0x1000 <= a < STACK + 0x10}
        assert changed <= allowed, (label, sorted(hex(a) for a in changed - allowed))
        freed = ('free',) in self.expected
        assert fault.code == 0, (label, fault.address, fault.code)
        assert result == (0 if freed else 1), (label, result)
        assert self.actual == self.expected, dict(case=label, actual=self.actual, expected=self.expected)
        assert self.native_state() == self.original_state(), dict(
            case=label, native=self.native_state(), original=self.original_state())
        if freed:  # the native module refuses a tick after the free
            self.fan.freed = 0
        return result


def make(fan=(1, 0, 0, 0, 0.0, 0.0), player=(0, 0, (7, 8, 9, 10), (0, 0, 0), 2.5), glob=(0, 0, 0xFF, 0)):
    lifecycle, phase, timer, flags2, spin, rot = fan
    b00, b0F, f70, pos, f224 = player
    return (Fan(lifecycle, phase, timer, flags2, spin, rot, 0),
            Player(b00, b0F, (C.c_float * 4)(*f70), (C.c_float * 3)(*pos), f224),
            Globals(*glob))


def captured_states():
    """(flags2, phase, timer, spin bits, rot.z bits) of the fans in each capture."""
    base = DECOMP/'build/startup-reference'
    states, images = set(), 0
    for name in CAPTURES:
        path = base/name
        if not path.exists(): continue
        memory = path.read_bytes(); images += 1
        for i in range(0x100):
            a = POOL + i * RECORD
            if struct.unpack_from('<I', memory, a + 0x10)[0] != ENTRY: continue
            assert memory[a + 4] == 1, (name, 'captured fan not in lifecycle 1')
            states.add((struct.unpack_from('<H', memory, a + 0x2E)[0], memory[a + 5],
                        struct.unpack_from('<h', memory, a + 0x28)[0],
                        struct.unpack_from('<I', memory, a + 0x38)[0],
                        struct.unpack_from('<I', memory, a + 0xC8)[0]))
    return states, images


def main():
    elf = (DECOMP/'config/SCUS_971.12').read_bytes()
    overlay = (DECOMP/'extract/OVERLAY/AREA11.BIN').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    assert len(overlay) == OVERLAY_SIZE and overlay[:4] == b'MWo3' and \
        struct.unpack_from('<I', overlay, 8)[0] == ARENA, 'not the original AREA11 overlay'
    out = ROOT/'build/fan_original_reference'; out.mkdir(parents=True, exist_ok=True)
    lib_path = out/'fan.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_fan_original.c', '-o', str(lib_path)],
                   cwd=ROOT, check=True)
    lib = C.CDLL(str(lib_path))
    lib.em_fan_original_tick.argtypes = [C.POINTER(Fan), C.POINTER(Player), C.POINTER(Globals),
                                         C.POINTER(Workers), C.POINTER(Fault)]
    lib.em_fan_original_spawn.argtypes = [C.POINTER(Fan), C.c_uint16, C.c_float]
    counts = dict(lifecycle=0, phase=0, box=0, globals=0, random=0, wrap=0, trajectory_ticks=0)
    STEP, MAX, SLOW = number(0x3B3EA2F2), number(0x3EB2B8C3), number(0x3D0EFA35)
    PI = number(0x40490FDB)

    # A. Lifecycles 0, 2, 3, 4, 0xFF (init incl. the bone-cap failure, free, no-op).
    for lifecycle, flags2, (bones, cap), rot in itertools.product(
            (0, 2, 3, 4, 0xFF), (0, 1, 2, 0x100), ((3, 0x40), (0x41, 0x40), (0x40, 0x40), (1, 0)),
            (0.0, 1.0)):
        fan, p, g = make((lifecycle, 3, 7, flags2, 0.25, rot))
        Case(elf, overlay, lib, fan, p, g, bones=bones, cap=cap).tick(('A', lifecycle, flags2, bones, cap))
        counts['lifecycle'] += 1

    # B. The phase machine and tail (player outside the box).
    spins = (0.0, -0.0, STEP, f32(0.0015), down(SLOW), SLOW, down(MAX), f32(MAX - STEP),
             down(f32(MAX - STEP)), MAX, f32(0.35), f32(-0.001))
    rots = (0.0, number(0x3F490FDB), PI, -PI, down(PI), up(-PI), f32(3.0), f32(-3.1), f32(1.83))
    pick = random.Random(0x8276B8)
    for phase, timer, flags2, spin, rot, g788 in itertools.product(
            range(7), (0, 1, 2, -1, -32768), (0, 1, 2), spins, rots, (0, 1, 2)):
        if g788 and not (phase == 1 and timer == 1 and flags2 == 0): continue
        fan, p, g = make((1, phase, timer, flags2, spin, rot), glob=(g788, 0, 0xFF, 0))
        Case(elf, overlay, lib, fan, p, g, sound=pick.choice((0, -1))).tick(('B', phase, timer, flags2, spin, rot, g788))
        counts['phase'] += 1

    # C. Box edges (flags2 1, phase >= 5 keeps spin; both arms of the 0.0349 selector).
    xs = (318.0, up(318.0), 330.0, down(340.0), 340.0)
    ys = (280.0, up(280.0), 300.0, down(320.0), 320.0)
    zs = (down(156.0), 156.0, 160.0, down(166.5), 166.5, 170.0)
    for spin, b00, x, y, z in itertools.product((0.0, down(SLOW), SLOW, 0.2), (0, 1, 3), xs, ys, zs):
        fan, p, g = make((1, 5, 0, 1, spin, 0.5), player=(b00, 9, (7, 8, 9, 10), (x, y, z), 2.5))
        Case(elf, overlay, lib, fan, p, g).tick(('C', spin, b00, x, y, z))
        counts['box'] += 1

    # D. Globals inside the box: B8 gate, 758 exit vs 7D8 bit, 7D8 preservation.
    for spin, b8, g758, g7d8, b00, z, flags2 in itertools.product(
            (0.0, SLOW), (0, 1, 2), (0xFF, 0, 0xFE), (0, 0x80, 0x41), (1, 0), (150.0, 160.0), (0, 1, 2)):
        fan, p, g = make((1, 5, 0, flags2, spin, 0.5), player=(b00, 9, (7, 8, 9, 10), (330, 300, z), 2.5),
                         glob=(0, b8, g758, g7d8))
        Case(elf, overlay, lib, fan, p, g, publish=b00).tick(('D', spin, b8, g758, g7d8, b00, z, flags2))
        counts['globals'] += 1

    # E. Random states.
    rng = random.Random(0x827630)
    for n in range(1500):
        fan, p, g = make((1, rng.randrange(6), rng.randrange(-3, 64), rng.choice((0, 1, 1, 2)),
                          f32(rng.uniform(-0.01, 0.36)), f32(rng.uniform(-3.2, 3.2))),
                         player=(rng.choice((0, 1, 1, 3)), rng.randrange(256),
                                 tuple(f32(rng.uniform(-2, 2)) for _ in range(4)),
                                 (f32(rng.uniform(310, 345)), f32(rng.uniform(275, 325)),
                                  f32(rng.uniform(150, 172))), f32(rng.uniform(0, 9))),
                         glob=(rng.randrange(3), rng.choice((0, 0, 1)), rng.choice((0xFF, 0xFF, 0, 7)),
                               rng.randrange(256)))
        Case(elf, overlay, lib, fan, p, g).tick(('E', n))
        counts['random'] += 1

    # F. Whole-cycle trajectories from spawn: original and native in lockstep,
    # the original keeping its own memory between ticks. The player walks a
    # path through the box so the exit, bit and hit arms fire mid-cycle.
    trajectory = {0: set(), 1: set()}
    for flags2 in (0, 1):
        for walk, g758 in ((False, 0xFF), (True, 0xFF), (True, 0)):
            fan, p, g = make(glob=(0, 0, g758, 0))
            lib.em_fan_original_spawn(C.byref(fan), flags2, 0.0)
            case = Case(elf, overlay, lib, fan, p, g)
            for t in range(1400):
                if walk:
                    case.player.pos[0], case.player.pos[1] = 330.0, 300.0
                    case.player.pos[2] = f32(150.0 + (t % 97) * 0.2)
                    case.player.b00 = 1
                    case.glob.d8106B8 = 0
                    case.push()
                case.tick(('F', flags2, walk, g758, t))
                counts['trajectory_ticks'] += 1
                if not walk:
                    trajectory[flags2].add((flags2, fan.phase, fan.timer, bits(fan.spin), bits(fan.rot_z)))

    # H. 001B1470 alone (original ELF instructions) against the native wrap.
    lib.em_fan_original_wrap_001B1470.argtypes = [C.c_float]
    lib.em_fan_original_wrap_001B1470.restype = C.c_float
    angles = [PI, -PI, down(PI), up(PI), up(-PI), down(-PI), 0.0, -0.0, f32(6.2831855),
              f32(-6.2831855), f32(9.5), f32(-9.5), f32(12.9), f32(-12.9)]
    angles += [f32(rng.uniform(-13, 13)) for _ in range(500)]
    for angle in angles:
        o = FanOracle(elf)
        o.run(0x1B1470, floats=(angle,))
        assert bits(lib.em_fan_original_wrap_001B1470(angle)) == o.f[0], ('wrap', angle)
    counts['wrap'] = len(angles)

    # G. The captured original RAM states lie on the (walk-free) trajectory.
    states, images = captured_states()
    assert images and states, 'no captured fan states found'
    missing = states - trajectory[0] - trajectory[1]
    assert not missing, ('captured fan states not reproduced', missing)

    report = dict(status='PASS', elf_sha256=ELF_SHA,
                  overlay_sha256=hashlib.sha256(overlay).hexdigest(), cases=counts,
                  captured_images=images, captured_states=len(states),
                  original_functions=['0x827630 (AREA11 overlay)', '001B0FD0', '001B0EA0', '001B1470'],
                  workers=['001B0FD0 model/bone part (001B0EA0 leaves, 001C62C0)', '001FBD50',
                           '001C6380', '001B17A0', '001B0C60', 'draw +0x4C', '001AFC10'])
    (out/'report.json').write_text(json.dumps(report, indent=2) + '\n')
    total = sum(v for k, v in counts.items() if k != 'trajectory_ticks')
    print(f"Original fan 0x827630: PASS {total} state cases, {counts['trajectory_ticks']} lockstep ticks, "
          f"{len(states)} captured states from {images} RAM images on the cycle")


if __name__ == '__main__':
    main()
