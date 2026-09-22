#!/usr/bin/env python3
"""Original door script sequencing, object command and timer callback oracle.

Runs BC350, BC0E0, BA1F0, B8020, B9BA0 and frame preparation from the user's
ELF. Player clip selection, camera retarget, source-animation stepping and
output devices are explicit ordered boundaries, verified separately.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from test_door_original_reference import Door, Hooks as OwnerHooks, CALL, KICK, ADVANCE, START, PUBLISH
from test_door_transit_reference import Plan
from test_interaction_frame_reference import Frame, Script, FIELDS
from test_item_sdk_math_reference import Original as ScalarOriginal
from test_pickup_owner_reference import OwnerOracle
from test_interaction_scan_reference import ELF_SHA
from test_point_light_reference import bits

ROOT = Path(__file__).resolve().parents[1]
ACTOR, DRAW, STATE, RECORD = 0x920000, 0x990000, 0x9201f0, 0x950000


class Original(ScalarOriginal):
    run = OwnerOracle.run

    def plain(self, word):
        if word >> 26 == 32:
            base, target = word >> 21 & 31, word >> 16 & 31
            address = (self.r[base] + C.c_int16(word & 65535).value) & 0xffffffff
            self.r[target] = C.c_int8(self.load(address, 1)).value & 0xffffffff
            return
        super().plain(word)


class Image(C.Structure):
    _fields_ = [('bytes', C.POINTER(C.c_ubyte)), ('base', C.c_uint32),
                ('entry', C.c_uint32), ('length', C.c_uint32)]


FRAME = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(Script), C.POINTER(C.c_ubyte))
PLAYER = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint16, C.c_float, C.c_float)
SOUND = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_float)
EMIT = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int)


class Hooks(C.Structure):
    _fields_ = [('context', C.c_void_p), ('frame', FRAME), ('camera', CALL),
                ('player', PLAYER), ('object', PLAYER), ('sound', SOUND)]


class Program(C.Structure):
    _fields_ = [('image', Image), ('script', Script), ('owner', C.POINTER(Door)),
                ('hooks', Hooks), ('failed', C.c_int)]


def sequence(elf, native, side, ready_tick):
    o = Original(elf)
    door = Door()
    door.status, door.class_flags, door.subtype, door.lifecycle, door.phase = 1, 0x85, 3, 1, 3
    door.armed, door.animation_flags = 4, 0x1234
    door.origin = (C.c_float*3)(423, 184.8, 290.3)
    for offset, value in ((0, 1), (2, 0x85), (3, 3), (4, 1), (5, 3), (11, 4)):
        o.save(ACTOR + offset, value, 1)
    o.save(ACTOR + 0x4c, DRAW)
    o.save(STATE + 14, 0x1234, 2)
    o.write(ACTOR + 0xb0, bytes(door.origin))
    expected, actual = [], []
    frame = Frame()

    def advance(r): expected.append(('advance',)); r.r[2] = 0x123
    def sound(r):
        assert (r.r[4], r.r[6], r.f[12]) == (ACTOR, 0, bits(300))
        expected.append(('sound', r.r[5], r.f[12]))
    def initialize(r):
        assert (r.r[4], r.f[13]) == (ACTOR, 0)
        expected.append(('object', r.r[5], r.f[12], r.f[13]))
    def player(r):
        assert r.r[4:6] == [ACTOR, STATE]
        expected.append(('player', r.load(r.r[6]+20, 2), bits(1), r.load(r.r[6]+12)))
        r.r[2] = 1
    def publish(r): expected.append(('publish',)); r.save(ACTOR + 1, 1, 1)
    o.calls.update({0x1c64f0: advance, 0x1fbd50: sound, 0x1c67e0: initialize,
        0x1b9a00: player, 0x1b7b30: lambda r: (expected.append(('camera',)), setattr_result(r, 1)),
        0x1d2610: lambda r: expected.append(('frame', 1)),
        0x1aeba0: lambda r: expected.append(('frame', 2)),
        0x1d25f0: lambda r: expected.append(('frame', 4)),
        0x1c68c0: lambda r: expected.append(('place',)), 0x1b1b30: publish,
        DRAW: lambda r: expected.append(('draw',))})

    @EMIT
    def emit(_, event): actual.append(('frame', event)); return 1
    @FRAME
    def frame_hook(_, script, record):
        raw = C.string_at(record, 64)
        return native.em_interaction_frame_command(C.byref(frame), script,
            struct.unpack_from('<I', raw, 8)[0], struct.unpack_from('<I', raw, 20)[0] != 0,
            emit, None)
    hooks = Hooks(None, frame_hook, CALL(lambda _: actual.append(('camera',)) or 1),
        PLAYER(lambda _, clip, rate, blend: actual.append(('player', clip, bits(rate), bits(blend))) or 1),
        PLAYER(lambda _, clip, blend, start: actual.append(('object', clip, bits(blend), bits(start))) or 1),
        SOUND(lambda _, cue, radius: actual.append(('sound', cue, bits(radius))) or 1))
    program = Program()
    assert native.em_door_program_load(C.byref(program),
        str(ROOT/'assets/scene_snow/door_original/program.emsc').encode(), C.byref(door), C.byref(hooks))
    plan = Plan(0x24de40, side, 0x43 if side else 0x45, 0 if side else 2,
                0x402 if side else 0x401, 70 if side else 90, 0, (C.c_float*4)(), 0)
    assert native.em_door_program_patch(C.byref(program), C.byref(plan))
    for address, value in ((0x24dc14, plan.player_clip), (0x24dc54, plan.door_clip),
                           (0x24dc58, plan.sound), (0x24dc8c, bits(plan.wait_ticks))):
        o.save(address, value)
    o.run(0x1ba1a0, (STATE, 0x24de40))
    assert native.em_door_program_start(C.byref(program), 0x24de40)
    def native_advance(_, flags):
        actual.append(('advance',)); flags[0] = 0x123; return 1
    owners = OwnerHooks(None, CALL(lambda _: 1), KICK(lambda *_: -1), ADVANCE(native_advance),
        CALL(lambda _: native.em_door_program_tick(C.byref(program))), START(lambda *_: -1),
        CALL(lambda _: -1), CALL(lambda _: -1), CALL(lambda _: actual.append(('place',)) or 1),
        PUBLISH(lambda *_: actual.append(('publish',)) or 1),
        CALL(lambda _: actual.append(('draw',)) or 1), CALL(lambda _: -1))
    ticks = 0
    while door.phase == 3 and ticks < 200:
        frame.player_ready = int(ticks >= ready_tick)
        o.save(0x70003b8f, frame.player_ready, 1)
        expected.clear(); actual.clear()
        o.run(0x1bc350, (ACTOR,))
        assert native.em_door_original_tick(C.byref(door), 1, 0, C.byref(owners)) == 1
        assert actual == expected, (side, ticks, actual, expected)
        assert (door.phase, door.animation_active, door.animation_flags) == (
            o.load(ACTOR + 5, 1), C.c_int8(o.load(STATE + 12, 1)).value,
            C.c_int16(o.load(STATE + 14, 2)).value)
        assert (program.script.active, program.script.phase, program.script.pc) == (
            C.c_int32(o.load(STATE)).value, o.load(STATE + 4), o.load(STATE + 8))
        for name, (address, size) in FIELDS.items():
            assert getattr(frame, name) == o.load(address, size), (ticks, name)
        assert C.string_at(program.image.bytes, program.image.length) == o.read(0x24dbc0, 0x3c0)
        ticks += 1
    assert door.phase == 4 and ticks < 200
    native.em_door_program_free(C.byref(program))
    return ticks


def setattr_result(original, value): original.r[2] = value


def main():
    elf = (ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    output = ROOT/'build/door_program_reference'
    output.mkdir(parents=True, exist_ok=True)
    library = output/'program.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
        '-shared', '-fPIC', '-Isrc', 'src/game/em_door_program.c', 'src/game/em_door_original.c',
        'src/game/em_interaction_frame.c', 'src/game/em_script.c', '-lm', '-o', str(library)],
        cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.em_door_program_load.argtypes = [C.POINTER(Program), C.c_char_p, C.POINTER(Door), C.POINTER(Hooks)]
    native.em_door_program_patch.argtypes = [C.POINTER(Program), C.POINTER(Plan)]
    native.em_door_program_start.argtypes = [C.POINTER(Program), C.c_uint32]
    native.em_door_program_tick.argtypes = native.em_door_program_free.argtypes = [C.POINTER(Program)]
    native.em_door_original_tick.argtypes = [C.POINTER(Door), C.c_int, C.c_uint8, C.POINTER(OwnerHooks)]
    native.em_interaction_frame_command.argtypes = [C.POINTER(Frame), C.POINTER(Script),
                                                   C.c_uint, C.c_int, EMIT, C.c_void_p]
    rows = [dict(side=side, player_ready_tick=ready, callbacks=sequence(elf, native, side, ready))
            for side in (0, 1) for ready in (0, 1, 3, 17)]
    report = dict(status='PASS', original_script_callbacks=sum(row['callbacks'] for row in rows), cases=rows,
        scope='Actual ordinary-door scripts/controller, object-init sound order, timer and frame readiness; '
              'camera, player animation, object source clock and output devices are ordered callback boundaries.')
    (output/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__': main()
