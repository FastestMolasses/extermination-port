#!/usr/bin/env python3
"""Execute the original footstep dispatch and compare em_player_floor.c.

WP-15 P14/P15. The user's pinned ELF supplies every instruction and table;
none are embedded here. The bounded interpreter (test_player_reversal_reference
Reversal, itself test_point_light_reference's Oracle) runs, unmodified:

  00187350  footstep dispatch, wet-feet timer and wade tail
  00187EE0  surface effect selection
  00182430  surface/gear sound selection
  00179B90  rand()&7 fold (its 00122BB8 rand is fed from the case's list)
  001031E0  vector copy

Hooked boundaries (recorded, never simulated): 001FBD50 positional sound,
001EFD90 effect spawn, 001F0460 decal with its SDK matrix builders
001029C0/00102BB0/00102B08/00102948, and 001E8B90 wade level.

Cases: every D_00248C90 row; random synthetic actors; the captured first
control run (tools collision_run_poll.json: 1024 actor bytes per original
frame, loaded as the actor's RAM) replayed frame by frame, which also checks
the step phase and wet timer the original left in each frame; and one call
over the whole captured playable EE RAM image (playable_ee.bin), with the
foot nodes read from the player's own node array.
"""
import ctypes as C
import hashlib
import json
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_player_reversal_reference import Reversal, ELF_SHA256  # noqa: E402
from test_point_light_reference import bits, number, signed  # noqa: E402

DECOMP = ROOT.parent / 'Extermination'
REFERENCE = DECOMP / 'build/startup-reference'
ACTOR = 0x680000
NODES = 0x690000        # synthetic node pointer array (D_00275B40 target)
NODE17, NODE18 = 0x691000, 0x692000
PLAYER = 0x8102B0       # the player actor in the captured RAM images
STEP_TABLE, STEP_ROWS = 0x248C90, 459

DISPATCH, EFFECT_SELECT, SOUND_SELECT = 0x187350, 0x187EE0, 0x182430
SOUND, EFFECT, DECAL, WADE = 0x1FBD50, 0x1EFD90, 0x1F0460, 0x1E8B90
IDENTITY, ROTATE_Y, ROTATE_X, COPY_QUAD = 0x1029C0, 0x102BB0, 0x102B08, 0x102948


class StepActor(C.Structure):
    _fields_ = [('position', C.c_float * 3), ('rotation', C.c_float * 3),
                ('clock', C.c_float), ('speed', C.c_float), ('slope', C.c_float),
                ('surface_y', C.c_float), ('anim_flags', C.c_uint32),
                ('clip', C.c_int16), ('wet', C.c_int16),
                ('mode', C.c_uint8), ('tier', C.c_uint8), ('step', C.c_uint8),
                ('surface', C.c_uint8), ('depth', C.c_uint8), ('contact', C.c_uint8),
                ('obstruction', C.c_uint8)]


class StepScene(C.Structure):
    _fields_ = [('foot17', C.POINTER(C.c_float)), ('foot18', C.POINTER(C.c_float)),
                ('frame', C.c_uint32), ('area', C.c_uint8)]


RANDOM5_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_uint))
RANDOM_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_uint32))
SOUND_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint)
EFFECT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_float), C.POINTER(C.c_float))
DECAL_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float), C.c_float, C.c_float)
WADE_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(C.c_float), C.c_float)


class StepWorkers(C.Structure):
    _fields_ = [('context', C.c_void_p), ('random5', RANDOM5_FN), ('random', RANDOM_FN),
                ('sound', SOUND_FN), ('effect', EFFECT_FN), ('decal', DECAL_FN),
                ('wade', WADE_FN)]


# (field, actor offset, size, float, signed)
FIELDS = (
    ('clock', 0x3C, 4, True), ('speed', 0x38, 4, True), ('slope', 0x9C, 4, True),
    ('surface_y', 0x250, 4, True), ('anim_flags', 0x200, 4, False),
    ('clip', 0x20C, 2, False), ('wet', 0x212, 2, False), ('mode', 0x1F0, 1, False),
    ('tier', 0x25C, 1, False), ('step', 0x25E, 1, False), ('surface', 0x23A, 1, False),
    ('depth', 0x23C, 1, False), ('contact', 0xA, 1, False), ('obstruction', 0x314, 1, False),
)


class Footstep(Reversal):
    def __init__(self, elf, ram=None):
        super().__init__(elf, b'')
        self.ram = ram
        self.log = []
        self.calls = {
            SOUND: lambda o: o.record('sound', o.r[4], o.r[5], o.r[6], o.f[12]),
            EFFECT: lambda o: o.record('effect', o.r[4], o.vector(o.r[5]), o.vector(o.r[6])),
            IDENTITY: lambda o: o.record('identity', o.r[4]),
            ROTATE_Y: lambda o: o.record('rotate_y', o.r[4], o.r[5], o.f[12]),
            ROTATE_X: lambda o: o.record('rotate_x', o.r[4], o.r[5], o.f[12]),
            COPY_QUAD: lambda o: o.record('translate', o.r[4], o.vector(o.r[5])),
            DECAL: lambda o: o.record('decal', o.r[4], o.r[5]),
            WADE: lambda o: o.record('wade', o.r[4], o.f[12]),
        }

    def load(self, address, size=4):
        if self.ram is not None and address not in self.mem and address < len(self.ram):
            if not 0x100000 <= address < 0x275b00:
                return int.from_bytes(self.ram[address:address + size], 'little')
        return super().load(address, size)

    def vector(self, address):
        return tuple(self.load(address + 4 * i) for i in range(3))

    def rng(self, values):
        self.rng_values = iter(values)
        self.rng_calls = 0


def build_native():
    out = ROOT / 'build/player_footstep_reference'
    out.mkdir(parents=True, exist_ok=True)
    lib = out / ('floor.dylib' if sys.platform == 'darwin' else 'floor.so')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_player_floor.c', '-lm', '-o', str(lib)],
                   cwd=ROOT, check=True)
    native = C.CDLL(str(lib))
    native.em_player_footstep_tick.argtypes = [C.POINTER(StepActor), C.POINTER(StepScene),
                                               C.POINTER(StepWorkers)]
    native.em_player_step_frames.argtypes = [C.c_int, C.POINTER(C.c_int), C.POINTER(C.c_int)]
    native.em_player_step_sound_base.argtypes = [C.c_uint8, C.c_uint8, C.c_uint8]
    native.em_player_step_sound_base.restype = C.c_uint
    return out, native


class NativeLog:
    """Native workers that record the original's hook entries exactly."""

    def __init__(self, rng_values, rotation_offset=0xC0, position_offset=0xB0):
        self.values = list(rng_values)
        self.used = 0
        self.entries = []
        self.rotation_offset = rotation_offset
        self.position_offset = position_offset
        self.workers = StepWorkers(None, RANDOM5_FN(self.random5), RANDOM_FN(self.random),
                                   SOUND_FN(self.sound), EFFECT_FN(self.effect),
                                   DECAL_FN(self.decal), WADE_FN(self.wade))

    def next_value(self):
        value = self.values[self.used]
        self.used += 1
        return value

    def random5(self, _, out):
        value = self.next_value() & 7            # 00179B90 over 00122BB8
        out[0] = value if value < 5 else value - 5
        return 0

    def random(self, _, out):
        out[0] = self.next_value() & 0xffffffff
        return 0

    def sound(self, _, sound_id):
        self.entries.append(('sound', 'actor', sound_id, 0, bits(300.0)))
        return 0

    def effect(self, _, effect_id, position, rotation):
        self.entries.append(('effect', effect_id, tuple(bits(position[i]) for i in range(3)),
                             tuple(bits(rotation[i]) for i in range(3))))
        return 0

    def decal(self, _, position, yaw, pitch):
        self.entries += [('identity',), ('rotate_y', bits(yaw)), ('rotate_x', bits(pitch)),
                         ('translate', tuple(bits(position[i]) for i in range(3))), ('decal', 1)]
        return 0

    def wade(self, _, position, level):
        self.entries.append(('wade', tuple(bits(position[i]) for i in range(3)), bits(level)))
        return 0


def normalize(oracle, actor_base):
    """Map raw hook records onto the native worker vocabulary."""
    out = []
    for entry in oracle.log:
        kind = entry[0]
        if kind == 'sound':
            _, actor, sound_id, zero, gain = entry
            assert actor == actor_base, hex(actor)
            out.append(('sound', 'actor', sound_id, zero, gain))
        elif kind == 'effect':
            _, effect_id, position, rotation = entry
            out.append(('effect', effect_id, position, rotation))
        elif kind == 'identity':
            assert entry[1] == 0x700036A0, hex(entry[1])
            out.append(('identity',))
        elif kind in ('rotate_y', 'rotate_x'):
            assert entry[1] == entry[2] == 0x700036A0
            out.append((kind, entry[3]))
        elif kind == 'translate':
            assert entry[1] == 0x700036D0
            out.append(('translate', entry[2]))
        elif kind == 'decal':
            assert entry[2] == 0x700036A0
            out.append(('decal', entry[1]))
        elif kind == 'wade':
            assert entry[1] == actor_base + 0xB0, hex(entry[1])
            out.append(('wade', oracle.vector(actor_base + 0xB0), entry[2]))
        else:
            raise AssertionError(entry)
    return out


def load_state(oracle, base, actor):
    for name, offset, size, is_float in FIELDS:
        value = getattr(actor, name)
        oracle.save(base + offset, bits(value) if is_float else value & ((1 << (8 * size)) - 1), size)
    for i in range(3):
        oracle.save(base + 0xB0 + 4 * i, bits(actor.position[i]))
        oracle.save(base + 0xC0 + 4 * i, bits(actor.rotation[i]))


def actor_from(oracle, base):
    actor = StepActor()
    for name, offset, size, is_float in FIELDS:
        raw = oracle.load(base + offset, size)
        if is_float:
            setattr(actor, name, number(raw))
        elif name in ('clip', 'wet'):
            setattr(actor, name, signed(raw, 16))
        else:
            setattr(actor, name, raw)
    for i in range(3):
        actor.position[i] = number(oracle.load(base + 0xB0 + 4 * i))
        actor.rotation[i] = number(oracle.load(base + 0xC0 + 4 * i))
    return actor


def compare(oracle, base, actor, label):
    step = oracle.load(base + 0x25E, 1)
    wet = signed(oracle.load(base + 0x212, 2), 16)
    assert actor.step == step, (label, 'step', actor.step, step)
    assert actor.wet == wet, (label, 'wet', actor.wet, wet)
    return step, wet


def run_case(elf, native, actor, feet, frame, area, rng_values, ram=None, base=ACTOR,
             node_array=NODES):
    oracle = Footstep(elf, ram)
    oracle.r[28] = 0x27D370
    if ram is None:
        load_state(oracle, base, actor)
        oracle.save(NODES + 0x44, NODE17); oracle.save(NODES + 0x48, NODE18)
        for node, foot in ((NODE17, feet[0]), (NODE18, feet[1])):
            for i in range(3):
                oracle.save(node + 0xC0 + 4 * i, bits(foot[i]))
    else:
        load_state(oracle, base, actor)  # the step phase under test
    oracle.save(0x275B40, node_array)
    oracle.save(0x70003B68, frame)
    oracle.save(0x810700, area, 1)
    oracle.rng(rng_values)
    oracle.call(DISPATCH, base)
    log = NativeLog(rng_values)
    f17 = (C.c_float * 3)(*feet[0]); f18 = (C.c_float * 3)(*feet[1])
    scene = StepScene(f17, f18, frame, area)
    assert native.em_player_footstep_tick(C.byref(actor), C.byref(scene), C.byref(log.workers)) == 0
    expected = normalize(oracle, base)
    assert expected == log.entries, ('calls', expected, log.entries)
    assert oracle.rng_calls == log.used, ('random calls', oracle.rng_calls, log.used)
    return oracle, log


def random_actor(rng):
    actor = StepActor()
    actor.mode = rng.choice([1, 2, 0x2F, 0x41, 1, 2, 0x36, 0x37, 0, 3, 4, 5, 6, 7, 0x1D, rng.randrange(256)])
    actor.clip = rng.choice([1, 2, 3, 0xB, 0xC, 0xD, 0x15, 0x16, 0x17, 0x4C, 0x4D, 0x4E,
                             0x148, 0x149, 0x14A, 0x14B, 0, 4, 5, 0x6A, rng.randrange(STEP_ROWS)])
    actor.clock = rng.choice([rng.uniform(-2, 140), float(rng.randrange(0, 140)), 72.0, 21.0, 3.0,
                              number(bits(26.0) + 1), number(bits(2.0) - 1)])
    actor.anim_flags = rng.choice([0, 0x1000, 0x2000, 0x8000, 0x3000, 0xffff8000, 0x4000, 0x0800, 0x1])
    actor.tier = rng.choice([0, 1, 2, 3, rng.randrange(256)])
    actor.step = rng.choice([0, 1, 2, 3, 0x81, 0x82, 0x83, 0x80, 0x8F, 0x80 | rng.randrange(128),
                             rng.randrange(256)])
    actor.surface = rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xC, 0xD, 0xE, 0xF, 0x35, 0x39,
                                0x5A, 0x5B, 0x5C, 0x5D, rng.randrange(256)])
    actor.depth = rng.choice([0, 0, 1, 2, rng.randrange(256)])
    actor.wet = rng.choice([0, 0, 1, 2, 0x78, rng.randrange(-5, 200)])
    actor.contact = rng.choice([0, 1, 2, 3, 0x81])
    actor.obstruction = rng.randrange(256)
    actor.speed = rng.choice([0.0, 0.05, 0.3, 0.8, rng.uniform(-1, 1)])
    actor.slope = rng.choice([0.0, 0.0016297102, rng.uniform(-1, 1)])
    actor.surface_y = rng.uniform(-50, 300)
    for i in range(3):
        actor.position[i] = rng.uniform(-400, 400)
        actor.rotation[i] = rng.uniform(-3.2, 3.2) if i == 1 else rng.choice([0.0, rng.uniform(-1, 1)])
    return actor


def main():
    elf = (DECOMP / 'config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA256, 'wrong original executable'
    out, native = build_native()
    rng = random.Random(0x187350)
    result = {}

    # 1. D_00248C90: every row's step frames (the dispatcher needs both).
    rows = 0
    for clip in range(STEP_ROWS):
        offset = STEP_TABLE + clip * 12 - 0x100000 + 0x300
        frame_a, frame_b = struct.unpack_from('<hh', elf, offset + 2)
        a, b = C.c_int(-1), C.c_int(-1)
        found = native.em_player_step_frames(clip, C.byref(a), C.byref(b))
        assert found == int(frame_a != 0 and frame_b != 0), (clip, frame_a, frame_b)
        if found: assert (a.value, b.value) == (frame_a, frame_b), clip
        rows += 1
    result['step_table_rows'] = rows

    # 2. 00182430 id selection over every surface/depth/tier byte, via the
    # original function with fixed variants (00179B90 = 0 for rand 0).
    selections = 0
    for surface in range(256):
        for depth in (0, 1, 2, 0xFF):
            if depth and surface != 0x5B: continue
            for tier in (0, 1, 2, 3, 4, 0x83, 0xFF):
                oracle = Footstep(elf); oracle.r[28] = 0x27D370
                oracle.save(ACTOR + 0x23A, surface, 1); oracle.save(ACTOR + 0x23C, depth, 1)
                oracle.rng([0, 5]); oracle.call(SOUND_SELECT, ACTOR, tier)
                ids = [entry[2] for entry in oracle.log]
                assert ids == [native.em_player_step_sound_base(surface, depth, tier & 0xFF), 0x138], \
                    (surface, depth, tier, ids)
                selections += 1
    result['sound_selection_cases'] = selections

    # 3. Random synthetic states.
    cases = fires = effects = decals = wades = 0
    for case in range(12000):
        actor = random_actor(rng)
        feet = [tuple(rng.uniform(-400, 400) for _ in range(3)) for _ in range(2)]
        frame = rng.randrange(1 << 20); area = rng.choice([0xB, 0xB, 0x15, 0x12])
        values = [rng.randrange(1 << 31) for _ in range(8)]
        oracle, log = run_case(elf, native, actor, feet, frame, area, values)
        compare(oracle, ACTOR, actor, ('random', case))
        kinds = [entry[0] for entry in log.entries]
        fires += kinds.count('sound') // 2; effects += kinds.count('effect')
        decals += kinds.count('decal'); wades += kinds.count('wade')
        cases += 1
    assert fires > 2000 and effects > 800 and decals > 50 and wades > 300, (fires, effects, decals, wades)
    result['random_cases'] = cases
    result['random_calls'] = dict(step_sound_pairs=fires, effects=effects, decals=decals, wades=wades)

    # 4. Captured first-control run: each original frame's actor bytes.
    capture = json.loads((REFERENCE / 'collision_run_poll.json').read_text())
    finals = {}
    for row in capture['rows']:
        finals[row['frame']] = bytes.fromhex(row['actor_hex'])  # last sample of each frame
    frames = sorted(finals)
    replayed = live = mailbox = 0
    fired_frames = []
    for previous, current in zip(frames, frames[1:]):
        if current != previous + 1: continue
        before, after = finals[previous], finals[current]
        actor_ram = bytearray(after)
        # The phase entering 00187350 is the previous frame's result, unless
        # 0017C030 posted its ending code this frame (mode 4/5 -> 0).
        step_in = before[0x25E]
        if before[0x1F0] in (4, 5) and after[0x1F0] == 0:
            step_in = 0x80 | before[0x25C]
            mailbox += 1
        actor_ram[0x25E] = step_in
        # The wet timer before this frame's tail.
        actor_ram[0x212:0x214] = before[0x212:0x214]
        # 00187350 runs before the 0015BCF0 tail publishes the hip into +B0.
        actor_ram[0xB0:0xBC] = after[0xA0:0xAC]
        oracle_ram = bytearray(0x900000)
        oracle_ram[PLAYER:PLAYER + len(actor_ram)] = actor_ram
        values = [rng.randrange(1 << 31) for _ in range(8)]
        feet = [tuple(rng.uniform(200, 260) for _ in range(3)) for _ in range(2)]
        # Synthetic node records for the captured actor's node array.
        node_array = 0x880000
        struct.pack_into('<II', oracle_ram, node_array + 0x44, 0x881000, 0x882000)
        for node, foot in ((0x881000, feet[0]), (0x882000, feet[1])):
            struct.pack_into('<3f', oracle_ram, node + 0xC0, *foot)
        feet = [struct.unpack_from('<3f', oracle_ram, node + 0xC0) for node in (0x881000, 0x882000)]
        probe = Footstep(elf, bytes(oracle_ram))
        actor = actor_from(probe, PLAYER)
        oracle, log = run_case(elf, native, actor, feet, current, 0xB, values,
                               ram=bytes(oracle_ram), base=PLAYER, node_array=node_array)
        step, wet = compare(oracle, PLAYER, actor, ('capture', current))
        assert step == after[0x25E], ('captured step', current, step, after[0x25E])
        assert wet == signed(struct.unpack_from('<H', after, 0x212)[0], 16), ('captured wet', current)
        if any(entry[0] == 'sound' for entry in log.entries):
            fired_frames.append(current)
        replayed += 1
        live += 1
    assert replayed > 100 and mailbox >= 1, (replayed, mailbox)
    result['captured_frames_replayed'] = replayed
    result['captured_mailbox_endings'] = mailbox
    result['captured_step_frames'] = fired_frames

    # 5. The whole captured playable RAM image (player at 0x8102B0).
    ram = (REFERENCE / 'playable_ee.bin').read_bytes()
    base_actor = actor_from(Footstep(elf, ram), PLAYER)
    ram_cases = 0
    for mode, clip, clock, step in ((1, 1, 70.0, 0), (1, 1, 20.0, 1), (2, 3, 1.0, 1), (4, 5, 3.0, 0x83),
                                    (0, 0, 40.0, 0x81), (1, 2, 30.0, 2)):
        patched = bytearray(ram)
        patched[PLAYER + 0x1F0] = mode
        struct.pack_into('<h', patched, PLAYER + 0x20C, clip)
        struct.pack_into('<f', patched, PLAYER + 0x3C, clock)
        patched[PLAYER + 0x25E] = step
        patched[PLAYER + 0x25C] = 3 if mode == 2 else 1
        probe = Footstep(elf, bytes(patched))
        actor = actor_from(probe, PLAYER)
        nodes = PLAYER + 0x110   # 001CB590 -> anim_bone_array_setup: player+0x110
        feet = []
        for index in (17, 18):
            node = struct.unpack_from('<I', patched, nodes + 4 * index)[0]
            feet.append(struct.unpack_from('<3f', patched, node + 0xC0))
        values = [rng.randrange(1 << 31) for _ in range(8)]
        oracle, log = run_case(elf, native, actor, feet, 4083, patched[0x810700], values,
                               ram=bytes(patched), base=PLAYER, node_array=nodes)
        compare(oracle, PLAYER, actor, ('ram', mode, clip, step))
        ram_cases += 1
        del base_actor
        base_actor = None
    result['captured_ram_cases'] = ram_cases

    # 6. A reached missing worker is a fault.
    faults = 0
    for name, fields in (('sound', dict(mode=1, clip=1, clock=10.0, step=0)),
                         ('effect', dict(mode=1, clip=1, clock=10.0, step=0, surface=5)),
                         ('decal', dict(mode=0, step=0x81, surface=0, wet=3)),
                         ('wade', dict(mode=0, step=0, depth=1, contact=1, speed=0.3, obstruction=0)),
                         ('random5', dict(mode=0, step=0x82))):
        actor = random_actor(rng)
        for key, value in fields.items(): setattr(actor, key, value)
        log = NativeLog([1] * 8)
        setattr(log.workers, name, type(getattr(log.workers, name))())
        scene = StepScene((C.c_float * 3)(), (C.c_float * 3)(), 4, 0xB)
        assert native.em_player_footstep_tick(C.byref(actor), C.byref(scene), C.byref(log.workers)) == -1, name
        faults += 1
    result['fault_cases'] = faults

    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print('player footstep original-instruction PASS', json.dumps(
        {k: v for k, v in result.items() if k != 'captured_step_frames'}))
    print('captured step frames:', result['captured_step_frames'])


if __name__ == '__main__':
    main()
