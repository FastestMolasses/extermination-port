#!/usr/bin/env python3
"""Compare original cinematic entry state and service order, not worker internals."""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import random
import subprocess

from test_interaction_frame_reference import Frame, Script, FIELDS, bits, number
from test_interaction_scan_reference import ELF_SHA
from test_item_sdk_math_reference import Original

ROOT = Path(__file__).resolve().parents[1]
STATE, RECORD = 0x900000, 0x901000
ZOOM_ZERO = number(0x43F02F4F)


class Inputs(C.Structure):
    _fields_ = [('fade_phase', C.c_int16), ('stream_ready', C.c_uint8)]


Emit = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int32)


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    folder = ROOT / 'build/interaction_cinematic_reference'
    folder.mkdir(parents=True, exist_ok=True)
    library = folder / 'cinematic.dylib'
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-shared', '-fPIC', '-Isrc', 'src/game/em_interaction_cinematic.c',
                    '-o', str(library)], cwd=ROOT, check=True)
    native = C.CDLL(str(library))
    command = native.em_interaction_cinematic_command
    command.argtypes = [C.POINTER(Frame), C.POINTER(Script), C.c_uint, C.c_int,
                        C.c_int32, C.POINTER(Inputs), Emit, C.c_void_p]
    rng = random.Random(0x1B82D0)
    cases = list(itertools.product(range(9, 13), range(6), (0, 1), (0, 1, 2),
                                  (0, 1), (0, 1, 2), (0, 1, 2), (0, 1)))
    cases += [(rng.randrange(9, 13), rng.randrange(256), rng.randrange(256),
               rng.randrange(256), rng.randrange(2), rng.randrange(-32768, 32768),
               rng.randrange(256), rng.randrange(2)) for _ in range(1000)]
    event_counts = [0] * 7
    for case, (sub, phase, ready, player, immediate, fade, stream_ready, attached) in enumerate(cases):
        frame = Frame(rng.randrange(4), player, ready, 4, 5, 6, 7, 3, 8, 9,
                      (C.c_uint8*12)(*range(12)), 321, 12, 470,
                      (C.c_float*4)(1, 2, 3, 4))
        # Original modifies only st[4], preserving the other phase bytes.
        script = Script(1, phase | 0x123400, 0x8283D0, rng.randrange(-128, 128), rng.randrange(256))
        input = Inputs(fade, stream_ready)
        stream = rng.choice((0, 0x65, 0x66, 0x1234567, -1))
        original = Original(elf)
        for name, (address, width) in FIELDS.items():
            original.save(address, getattr(frame, name), width)
        for i, value in enumerate(frame.activity): original.save(0x8106D4+i, value, 1)
        for i, value in enumerate(frame.up): original.save(0x8105F0+4*i, bits(value))
        original.save(STATE+4, script.phase)
        original.save(STATE+12, script.skip_phase, 1)
        original.save(0x70003B91, script.skip_request, 1)
        original.save(RECORD+8, sub)
        original.save(RECORD+20, immediate)
        original.save(RECORD+24, stream)
        original.save(0x28A9A0, fade, 2)
        original.save(0x8106F4, stream_ready, 1)
        original_events, native_events = [], []
        zoom = frame.zoom

        def original_frame():
            result = Frame.from_buffer_copy(frame)
            for name, (address, width) in FIELDS.items():
                setattr(result, name, original.load(address, width))
            for i in range(12): result.activity[i] = original.load(0x8106D4+i, 1)
            for i in range(4): result.up[i] = number(original.load(0x8105F0+4*i))
            result.zoom = zoom
            return result

        def service(event, argument):
            nonlocal zoom
            original_events.append((event, argument, bytes(original_frame()),
                                    original.load(STATE+4), original.load(STATE+12, 1),
                                    original.load(0x70003B91, 1)))
            if event == 4 and attached: original.save(0x70003B8F, 2, 1)
            if event == 5: zoom = ZOOM_ZERO

        def fade_out(o):
            assert (o.r[4], o.r[5]) == (4, 0)
            service(0, 0)
        def request(o):
            assert o.r[4] & 0xffffffff == stream & 0xffffffff
            service(1, stream)
            o.r[2] = 1
        def mute(o):
            assert o.r[4] in (0, 1) and (o.r[5], o.r[6]) == (0, 0)
            service(2, o.r[4])
        def bars(o):
            assert o.r[4] == 255
            service(3, 0)
        def attach(o):
            assert o.r[4] == 0x8102B0
            service(4, 0)
        def scope(o):
            assert o.f[12] == 0
            service(5, 0)
        def fade_in(o):
            assert (o.r[4], o.r[5]) == (16, 0)
            service(6, 0)
        original.calls.update({0x1AEDE0: fade_out, 0x1FD4C0: request, 0x119828: mute,
                               0x1AEB60: bars, 0x1B81D0: attach, 0x1D2610: scope,
                               0x1AEE10: fade_in})
        original.run(0x1B82D0, (0, STATE, RECORD))
        expected = original_frame()

        @Emit
        def emit(_, event, argument):
            native_events.append((event, argument, bytes(frame), script.phase,
                                  script.skip_phase & 255, script.skip_request))
            event_counts[event] += 1
            if event == 4 and attached: frame.player_ready = 2
            if event == 5: frame.zoom = ZOOM_ZERO
            return 1
        result = command(C.byref(frame), C.byref(script), sub, immediate, stream,
                         C.byref(input), emit, None)
        assert bytes(frame) == bytes(expected), (case, cases[case], 'frame')
        assert (script.phase, script.skip_phase & 255, script.skip_request, result) == (
            original.load(STATE+4), original.load(STATE+12, 1),
            original.load(0x70003B91, 1), original.r[2]), (case, cases[case], 'script')
        assert native_events == original_events, (case, cases[case], 'ordered service observations')

    # Native failed workers must stop the command at that boundary.
    failure_cases = 0
    for phase, events in ((0, [0, 1, 2, 2]), (1, [3]), (2, [4, 5]), (3, [6])):
        for rejected in range(len(events)):
            frame = Frame(); frame.player_ready = 1
            script = Script(1, phase, 0, 0, 0)
            input = Inputs(2, 1)
            called = []
            @Emit
            def fail(_, event, argument):
                called.append(event)
                return int(len(called)-1 != rejected)
            assert command(C.byref(frame), C.byref(script), 12, 0, 0x65,
                           C.byref(input), fail, None) == -1
            assert called == events[:rejected+1]
            failure_cases += 1
    report = {'original_state_and_service_order_cases': len(cases),
              'event_counts': event_counts, 'failed_worker_cases': failure_cases,
              'scope': 'full command instructions; fade/audio/skeleton/projection workers intercepted'}
    (folder / 'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Original cinematic entry PASS:', json.dumps(report))


if __name__ == '__main__': main()
