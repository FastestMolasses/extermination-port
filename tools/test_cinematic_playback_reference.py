#!/usr/bin/env python3
"""The scripted camera timeline against the original instructions:
0022EC30 (the start), 0022EEF0 (one frame) and 001B7B30 sub 0 (the wait),
em_cinematic_playback.c (docs/CAMERA_LIVE.md section 5).

The oracle executes the user's pinned ELF over a RAM image: the camera block
D_008101E0 (+0x6E scene, +0x70 track, +0x74 cursor, +0x78 head, +0x7C /
+0x80 / +0x84 event cursors, +0x88..+0x8A their states), the track at
+0x70 (the extract's bank bytes), the ELF's event tables (D_0026AE00 for
scene 0x22) and the playback's outputs D_008105D0..FF, 0x70003B60 (the
zoom 001D25F0 stores), D_00275BFC, D_008106F3 and D_00275C98. The SDK
tangent, the rotation, 001C7C00 and float_to_int run as original code; the
calls the translation emits are hooked and compared in order with their
arguments and the state at the call:
  A. starts: 0022EC30 for scene ids 0..0x25; the native start must equal
     its +0x7C..+0x8A and D_00275C98 for every admitted scene and refuse
     every other one;
  B. scene 1 (Roger's encounter, bank 0x96's clip 0): single frames at
     the time boundaries, a sample of the track and the roll x fov
     projection boundaries, and the wait 001B7B30 sub 0;
  C. scene 0x22 (the AREA11 opening, bank 0x98's clip 0, D_0026AE00):
     single frames over the +0x80 states, and sequences from the start
     (the -1 record, the cue 001B1E20(6, 0) at 1.0) and over the fade-out
     at 634 to the end (EM_TEST_FULL=1: the whole timeline);
  D. the three tracks' other records over tables of their own (the +0x7C
     effect / grey / clock, the +0x80 colours, unknown negatives and a
     truncated speed, the +0x84 flag), a sequence on scene 1.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess

from test_camera_reference import Track
from test_item_sdk_math_reference import Original
from test_interaction_scan_reference import ELF_SHA
from test_point_light_reference import bits, number
from reference_mode import FULL, MODE, banner, part, select

ROOT = Path(__file__).resolve().parents[1]
CAMERA, TABLE, SYNTH = 0x8101E0, 0x1400000, 0x1500000
CLOCK = 0x0123456789ABCDEF
OPENING_EVENTS = 0x26AE00


class Projection(C.Structure):
    _fields_ = [('tangent', C.c_float*13)]


class Events(C.Structure):
    _fields_ = [('base', C.c_uint32), ('words', C.POINTER(C.c_float)), ('count', C.c_uint32)]


class Playback(C.Structure):
    _fields_ = [('track', C.POINTER(Track)), ('events', C.POINTER(Events)), ('scene', C.c_int16),
                ('time', C.c_float), ('cursor', C.c_uint32*3), ('state', C.c_uint8*3),
                ('start_clock', C.c_uint64),
                ('eye', C.c_float*4), ('target', C.c_float*4), ('up', C.c_float*4),
                ('zoom', C.c_float), ('cut_counter', C.c_uint32), ('auxiliary', C.c_uint8),
                ('arg', C.c_int32*2), ('value', C.c_float)]


Emit = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.POINTER(Playback))
PUBLISH, RESTORE, EFFECT_OFF, FLAG_OFF, RUMBLE, FADE_OUT, FADE_IN, EFFECT, GREY, EFFECT_CLOCK, FLAG_ON = range(11)
PLAIN = {0, 1, 3, 4, 9, 11, 14, 18, 36}


def s32(v): return v - (1 << 32) if v & 0x80000000 else v
def s64(v):
    v &= (1 << 64) - 1
    return v - (1 << 64) if v >> 63 else v


def view(p):
    return (bytes(p.eye), bytes(p.target), bytes(p.up), bits(p.zoom), bits(p.time),
            p.cut_counter, p.auxiliary, tuple(p.cursor), bytes(p.state))


def original_view(o):
    return (o.read(0x8105D0, 16), o.read(0x8105E0, 16), o.read(0x8105F0, 16),
            o.load(0x70003B60), o.load(CAMERA+0x74), o.load(0x275BFC), o.load(0x8106F3, 1),
            tuple(o.load(CAMERA+0x7C+4*i) for i in range(3)), o.read(CAMERA+0x88, 3))


def native_args(kind, p):
    if kind in (PUBLISH, RESTORE): return ()
    if kind == EFFECT_OFF: return (0, 0, 0)
    if kind == EFFECT: return (5, 0, bits(p.value))
    if kind in (FLAG_OFF, FLAG_ON): return (2, 0 if kind == FLAG_OFF else 1)
    if kind in (RUMBLE, FADE_OUT, FADE_IN): return (p.arg[0], p.arg[1])
    if kind == GREY: return (0x80, 0x80, 0x80)
    return (s64(p.start_clock),)


def hooks(o, log):
    """The calls 0022EEF0 makes that the translation emits, with their
    arguments and the state at the call."""
    def record(kind, args):
        log.append((kind, args, original_view(o)))
    a = lambda i: s32(o.r[4+i] & 0xffffffff)
    o.calls[0x1DD980] = lambda _: record(PUBLISH, ())
    o.calls[0x1B0250] = lambda _: record(RESTORE, ())
    o.calls[0x21B9A0] = lambda _: record(EFFECT_OFF if a(0) == 0 else EFFECT,
                                         (a(0), o.f[12] & 0xffffffff, o.f[13] & 0xffffffff))
    o.calls[0x1D2830] = lambda _: record(FLAG_OFF if a(1) == 0 else FLAG_ON, (a(0), a(1)))
    o.calls[0x1B1E20] = lambda _: record(RUMBLE, (a(0), s64(o.r[5])))
    o.calls[0x1AEDE0] = lambda _: record(FADE_OUT, (a(0), a(1)))
    o.calls[0x1AEE10] = lambda _: record(FADE_IN, (a(0), a(1)))
    o.calls[0x21BA80] = lambda _: record(GREY, (a(0), a(1), a(2)))
    o.calls[0x21BA70] = lambda _: record(EFFECT_CLOCK, (s64(o.r[4]),))


def main():
    elf = (ROOT.parent/'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    data = (ROOT.parent/'Extermination/extract/chunk15/f12_id44.bin').read_bytes()
    roger_at = 0x41000+(struct.unpack_from('<I', data, 0x41004)[0] & ~3)
    opening_at = 0xD0800+(struct.unpack_from('<I', data, 0xD0804)[0] & ~3)
    out = ROOT/'build/cinematic_playback_reference'
    out.mkdir(parents=True, exist_ok=True)
    library = out/'camera.dylib'
    sources = ['src/game/em_cinematic_playback.c', 'src/game/em_cinematic_camera.c', 'src/game/em_camera_rotation.c',
               'src/game/em_owner_services_original.c', 'src/game/em_effect_original.c']
    subprocess.run(['cc', '-shared', '-fPIC', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-Isrc', *sources, '-lm', '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    native.em_cinematic_camera_load.argtypes = [C.POINTER(Track), C.c_char_p]
    native.em_cinematic_camera_free.argtypes = [C.POINTER(Track)]
    native.em_cinematic_projection_load.argtypes = [C.POINTER(Projection), C.c_char_p]
    native.em_cinematic_projection_zoom.argtypes = [C.POINTER(Projection), C.c_float]
    native.em_cinematic_projection_zoom.restype = C.c_float
    native.em_cinematic_events_load.argtypes = [C.POINTER(Events), C.POINTER(C.c_float), C.c_size_t, C.c_uint32,
                                                C.c_char_p]
    native.em_cinematic_playback_start.argtypes = [C.POINTER(Playback), C.POINTER(Track), C.POINTER(Events), C.c_int,
                                                   C.c_uint64]
    native.em_cinematic_playback_tick.argtypes = [C.POINTER(Playback), C.POINTER(Projection), Emit, C.c_void_p]
    native.em_cinematic_playback_wait.argtypes = [C.POINTER(Playback), Emit, C.c_void_p]
    roger, opening, projection = Track(), Track(), Projection()
    assets = ROOT/'assets/scene_snow'
    assert native.em_cinematic_camera_load(C.byref(roger), str(assets/'roger/encounter_camera.emcc').encode()) == 0
    assert native.em_cinematic_camera_load(C.byref(opening), str(assets/'opening_camera.emcc').encode()) == 0
    assert native.em_cinematic_projection_load(C.byref(projection),
                                               str(assets/'roger/camera_projection.emcp').encode())
    assert bytes(projection) == elf[0x26C598-0x100000+0x300:0x26C598-0x100000+0x300+52]
    # The exported tracks are the extract's banks.
    for track, at in ((roger, roger_at), (opening, opening_at)):
        assert struct.unpack_from('<f', data, at)[0] == track.duration
        assert data[at+16:at+16+32*track.sample_count] == bytes(
            C.cast(track.samples, C.POINTER(C.c_ubyte*(32*track.sample_count))).contents)
    words = (C.c_float*64)()
    opening_events = Events()
    assert native.em_cinematic_events_load(C.byref(opening_events), words, 64, OPENING_EVENTS,
                                           str(assets/'opening.emfx').encode()) == 0
    count = opening_events.count
    assert bytes(words)[:4*count] == elf[OPENING_EVENTS-0x100000+0x300:OPENING_EVENTS-0x100000+0x300+4*count]
    # D: the other records over tables of their own (scene 1 carries no
    # table, so they ride on it): the +0x7C effect, off, grey, clock; the
    # +0x80 colours, an unknown negative and a truncated speed; the +0x84 flag.
    t7c = [5.0, 0.25, 10.0, 20.0, -1.0, 30.0, 0.0]
    t80 = [-2.0, 4.0, 12.0, -4.0, 6.0, 33.75, -7.0, -3.0, 7.0, 8.0, -1.0, 9.0, 2.0, 10.0, 7.9, 0.0]
    t84 = [3.0, 7.0, 11.0, 0.0]
    synth_words = t7c + t80 + t84
    synth_c = (C.c_float*len(synth_words))(*synth_words)
    synth = Events(SYNTH, C.cast(synth_c, C.POINTER(C.c_float)), len(synth_words))
    synth_cursors = (SYNTH, SYNTH+4*len(t7c), SYNTH+4*(len(t7c)+len(t80)))
    tracks = {1: (roger, roger_at), 0x22: (opening, opening_at)}

    def oracle(scene):
        track, at = tracks[1 if scene != 0x22 else 0x22]
        o = Original(elf)
        o.write(TABLE, data[at:at+16+32*track.sample_count])
        o.save(CAMERA+0x6E, scene, 2)
        o.save(CAMERA+0x70, TABLE)
        o.save(CAMERA+0x78, bits(track.duration))
        o.write(SYNTH, struct.pack(f'<{len(synth_words)}f', *synth_words))
        return o

    def native_playback(scene, events):
        track = tracks[1 if scene != 0x22 else 0x22][0]
        p = Playback()
        assert native.em_cinematic_playback_start(C.byref(p), C.byref(track), events, scene, CLOCK)
        return p

    def seed(o, p, time, cursors=None, states=None):
        p.time = time
        if cursors is not None:
            for i in range(3): p.cursor[i] = cursors[i]
        if states is not None:
            for i in range(3): p.state[i] = states[i]
        p.eye[:] = (1, 2, 3, 7)
        p.target[:] = (4, 5, 6, 9)
        p.up[:] = (.25, -.75, .5, 3)
        p.zoom, p.cut_counter, p.auxiliary = 321, 17, 9
        o.save(CAMERA+0x74, bits(time))
        for i in range(3): o.save(CAMERA+0x7C+4*i, p.cursor[i])
        o.write(CAMERA+0x88, bytes(p.state))
        o.write(0x275C98, struct.pack('<Q', p.start_clock))
        for address, values in ((0x8105D0, p.eye), (0x8105E0, p.target), (0x8105F0, p.up)):
            o.write(address, bytes(values))
        o.save(0x70003B60, bits(p.zoom))
        o.save(0x275BFC, p.cut_counter)
        o.save(0x8106F3, p.auxiliary, 1)

    emitted = set()

    def tick(o, p, where):
        olog, nlog = [], []
        hooks(o, olog)

        @Emit
        def emit(_context, kind, state):
            nlog.append((kind, native_args(kind, state.contents), view(state.contents)))
            return 1
        o.run(0x22EEF0, (CAMERA,))
        result = native.em_cinematic_playback_tick(C.byref(p), C.byref(projection), emit, None)
        assert result in (0, 1), (where, result)
        assert [(k, a) for k, a, _ in olog] == [(k, a) for k, a, _ in nlog], ('calls', where, olog, nlog)
        assert [v for _, _, v in olog] == [v for _, _, v in nlog], ('state at a call', where)
        actual, expected = view(p), original_view(o)
        assert actual == expected, ('state', where, [(i, a, b) for i, (a, b) in enumerate(zip(actual, expected))
                                                     if a != b])
        assert o.read(0x275C98, 8) == struct.pack('<Q', p.start_clock), where
        emitted.update((k, a) for k, a, _ in olog)
        return result, len(olog)

    # A. starts: the scene ids 0022EC30's table covers (0..0x25). A scene
    # the translation refuses binds a table or has a 0022EEF0 cue.
    cued = {15, 22, 24, 25, 26, 27, 29, 30, 31, 33, 34, 35}
    starts = 0
    for scene in range(0x26):
        o = Original(elf)
        o.calls[0x21BAB0] = lambda oo: oo.r.__setitem__(2, CLOCK)
        o.write(CAMERA+0x7C, b'\x5a'*0xF)
        o.save(CAMERA+0x6E, scene, 2)
        o.run(0x22EC30, (CAMERA,))
        assert o.read(0x275C98, 8) == struct.pack('<Q', CLOCK)
        p = Playback()
        for i in range(3): p.cursor[i] = 0x5a5a5a5a
        for i in range(3): p.state[i] = 0x5a
        track = opening if scene == 0x22 else roger
        admitted = native.em_cinematic_playback_start(C.byref(p), C.byref(track), C.byref(opening_events),
                                                      scene, CLOCK)
        bound = o.read(CAMERA+0x7C, 12) != bytes(12)
        assert admitted == (scene in PLAIN or scene == 0x22), (scene, admitted)
        assert admitted or bound or scene in cued, scene
        assert not admitted or scene == 0x22 or (not bound and scene not in cued), scene
        if admitted:
            assert tuple(p.cursor) == tuple(o.load(CAMERA+0x7C+4*i) for i in range(3)), scene
            assert bytes(p.state) == o.read(CAMERA+0x88, 3) and p.start_clock == CLOCK, scene
            assert p.scene == scene and p.track
        starts += 1

    # B. scene 1: single frames.
    rng = random.Random(0x22EEF0)
    times = [-1, 0, .5, 25, 25.5, 690.5, 691, 692]
    times += [i*.5 for i in range(1382)]
    times += [rng.uniform(0, 691) for _ in range(80)]
    cases = [(time, None, None) for time in times]
    cases += [(.25, roll, fov) for roll in (-180, -90, -1, 0, 1, 90, 180)
              for fov in (-100, -.1, .1, .5, 20, 45, 46, 100)]
    total_b = len(cases)
    cases = select(cases, 300, 0x22EC30, keep=lambda i, c: i < 8 or c[1] is not None)
    calls = waits = 0
    captured = None
    for index, (time, roll, fov) in enumerate(cases):
        o = oracle(1)
        p = native_playback(1, None)
        original_sample = [roger.samples[i] for i in range(16)]
        if roll is not None:
            # Synthetic projection boundaries retain the real driver and
            # camera format; original exported sample data stay untouched.
            for sample in range(2):
                o.save(TABLE+16+32*sample+24, bits(roll))
                o.save(TABLE+16+32*sample+28, bits(fov))
                roger.samples[8*sample+6] = roll
                roger.samples[8*sample+7] = fov
        seed(o, p, time)
        result, n = tick(o, p, ('scene 1', index, time))
        assert result == int(0 <= time < 691), (index, time, result)
        calls += n
        if time == 25 and roll is None:
            ram = (ROOT.parent/'Extermination/build/startup-reference/roger-encounter/eeMemory.bin').read_bytes()
            world = struct.unpack_from('<I', ram, 0x275670)[0]
            assert bytes(p.up) == ram[0x8105F0:0x810600]
            assert bits(p.zoom) == struct.unpack_from('<I', ram, world+0x2468)[0]
            captured = {'camera_time': 25, 'up_bytes_exact': True, 'zoom_bytes_exact': True, 'zoom': p.zoom}
        if roll is None:
            olog, nlog = [], []
            hooks(o, olog)

            @Emit
            def emit(_context, kind, state):
                nlog.append((kind, native_args(kind, state.contents), view(state.contents)))
                return 1
            o.save(0x1300000+8, 0)
            o.run(0x1B7B30, (0, 0, 0x1300000))
            result = native.em_cinematic_playback_wait(C.byref(p), emit, None)
            assert result == o.r[2] and [(k, a) for k, a, _ in olog] == [(k, a) for k, a, _ in nlog]
            assert view(p) == original_view(o)
            waits += 1
            calls += len(olog)
        for i, value in enumerate(original_sample): roger.samples[i] = value

    # C. scene 0x22: single frames over the +0x80 states.
    # (A cursor never rests on a terminator: the step clears it there.)
    c80 = [(OPENING_EVENTS, 0), (OPENING_EVENTS+4, 1), (0, 0), (OPENING_EVENTS+4, 0x81), (OPENING_EVENTS+4, 0x80),
           (OPENING_EVENTS+4, 0), (OPENING_EVENTS, 0x81)]
    times = [-1, 0, .5, 1, 1.5, 134.5, 135, 633.5, 634, 634.5, 645.5, 646, 647]
    singles = [(t, c) for t in times for c in c80]
    singles += [(rng.uniform(0, 646), c80[rng.randrange(len(c80))]) for _ in range(60)]
    total_c = len(singles)
    singles = select(singles, 80, 0x22, keep=lambda i, c: c[0] in (0, 1, 634, 646))
    for index, (time, (cursor, state)) in enumerate(singles):
        o = oracle(0x22)
        p = native_playback(0x22, C.byref(opening_events))
        seed(o, p, time, (0, cursor, 0), (0, state, 0))
        _, n = tick(o, p, ('scene 0x22', index, time, hex(cursor), state))
        calls += n
    # C. scene 0x22: sequences from the start (as 001B8FC0 kind 6 leaves the
    # block: cursor 0) and over the fade-out to past the end.
    windows = [range(0, 1300)] if FULL else [range(0, 6), range(1264, 1300)]
    sequence_ticks = 0
    for window in windows:
        o = oracle(0x22)
        p = native_playback(0x22, C.byref(opening_events))
        seed(o, p, 0.0, tuple(p.cursor), tuple(p.state))
        o.calls[0x21BAB0] = lambda oo: oo.r.__setitem__(2, CLOCK)
        o.run(0x22EC30, (CAMERA,))
        first = window[0]
        if first:
            # Fast-forward the playback to the window: both cursors as the
            # sequence before leaves them (the -1 record taken at 0).
            p.time = first*.5
            p.cursor[1], p.state[1] = OPENING_EVENTS+4, 1
            o.save(CAMERA+0x74, bits(p.time))
            o.save(CAMERA+0x80, OPENING_EVENTS+4)
            o.save(CAMERA+0x89, 1, 1)
        seen = set()
        for k in window:
            _, n = tick(o, p, ('scene 0x22 sequence', k))
            sequence_ticks += 1
            calls += n
            seen.add(p.cursor[1])
        assert (0 in seen) == (window[-1] >= 1269), seen
    assert (RUMBLE, (6, 0)) in emitted and (FADE_OUT, (16, 0)) in emitted, sorted(emitted)
    # D. the other records, as a sequence on scene 1.
    o = oracle(1)
    p = native_playback(1, C.byref(synth))
    seed(o, p, 0.0, synth_cursors, (0, 0, 0))
    p.start_clock = CLOCK
    o.write(0x275C98, struct.pack('<Q', CLOCK))
    emitted.clear()
    for k in range(80):
        _, n = tick(o, p, ('tables', k))
        calls += n
        sequence_ticks += 1
    assert tuple(p.cursor) == (0, 0, 0), tuple(p.cursor)
    assert {(k, a) for k, a in emitted if k != PUBLISH} == {
        (EFFECT, (5, 0, bits(0.25))), (EFFECT_OFF, (0, 0, 0)), (GREY, (0x80, 0x80, 0x80)),
        (EFFECT_CLOCK, (s64(CLOCK),)), (FADE_OUT, (12, 1)), (FADE_IN, (33, 1)), (FADE_IN, (8, 0)),
        (FADE_OUT, (2, 0)), (FADE_IN, (7, 0)), (FLAG_ON, (2, 1)), (FLAG_OFF, (2, 0))}, sorted(emitted)

    native.em_cinematic_camera_free(C.byref(roger))
    native.em_cinematic_camera_free(C.byref(opening))
    raw = (assets/'roger/camera_projection.emcp').read_bytes()
    invalid = [raw[:11], raw[:-1], raw+b'\0', b'BAD!'+raw[4:],
               raw[:8]+struct.pack('<I', 14)+raw[12:],
               raw[:12]+struct.pack('<I', 0x7FC00000)+raw[16:]]
    for i, value in enumerate(invalid): (out/f'bad_{i}.emcp').write_bytes(value)
    raw = (assets/'opening.emfx').read_bytes()
    invalid = [raw[:11], raw[:-1], raw+b'\0', b'BAD!'+raw[4:], raw[:4]+struct.pack('<I', 2)+raw[8:],
               raw[:-4]+struct.pack('<f', 5.0), raw[:12]+struct.pack('<I', 0x7FC00000)+raw[16:],
               raw[:8]+struct.pack('<I', 65)+raw[12:]]
    for i, value in enumerate(invalid): (out/f'bad_{i}.emfx').write_bytes(value)
    executable = out/'sanitizer_test'
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                    '-fsanitize=address,undefined', '-Isrc', 'tests/cinematic_playback_test.c', *sources, '-lm',
                    '-o', str(executable)], cwd=ROOT, check=True)
    result = subprocess.run([str(executable)], cwd=ROOT, check=True, text=True, stdout=subprocess.PIPE)
    (out/'sanitizer.log').write_text(result.stdout)
    banner(f'{starts} starts (scene ids 0..0x25)',
           part(len(cases), total_b, 'scene-1 frames (all 8 time boundaries, all 56 roll x fov)'),
           part(len(singles), total_c, 'scene-0x22 frames (every +0x80 state at every boundary)'),
           f'{sequence_ticks} sequence ticks' + ('' if FULL else ' (the opening\'s start and end windows)'),
           'the captured t=25 comparison, malformed projections / tables and the sanitizer run in full')
    report = {'mode': MODE, 'starts': starts, 'scene1_frames': len(cases), 'scene1_waits': waits,
              'scene22_frames': len(singles), 'sequence_ticks': sequence_ticks, 'ordered_calls': calls,
              'exact_state_bytes': True, 'original_capture': captured, 'sanitizer': 'PASS',
              'scope': '0022EC30 for scene ids 0..0x25; 0022EEF0 for scenes 1 and 0x22 and the three tracks\' '
                       'records; 001B7B30 sub 0'}
    (out/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Original cinematic timeline PASS:', json.dumps(report))


if __name__ == '__main__': main()
