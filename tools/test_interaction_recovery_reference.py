#!/usr/bin/env python3
"""Execute original recovery countdown, frame ordering, and Use inhibition.

The camera/actor rendering and geometry workers are ordered boundaries.
0018B9C0, both frame drivers, and184BA0 execute the user's original ELF.
The countdown/order assertions document the host insertion contract; the
Use gate is compared with the actual native scan. No countdown module is
needed for the single guarded decrement at the real camera stage.
No original instruction bytes are embedded in this test or the native game.
"""
import ctypes as C
import hashlib
import itertools
import json
from pathlib import Path
import subprocess
import tempfile

from test_interaction_scan_reference import (
    ScanOracle, State, Candidate, PREDICATE, arbitration, ELF_SHA,
)
from test_status_frame_reference import Original as StatusOracle, Status

ROOT = Path(__file__).resolve().parents[1]
CAMERA, LOCK = 0x8101E0, 0x8106EF
CAMERA_CALLS = (0x1B1240, 0x18CE60, 0x18C0C0, 0x18D7B0,
                0x18C0D0, 0x22EEF0, 0x191390, 0x18BC20)


def camera_oracle(elf, remaining, state, top, events):
    original = ScanOracle(elf)
    original.save(LOCK, remaining, 1)
    original.save(CAMERA, state, 1)
    original.save(CAMERA + 4, top, 1)
    original.save(CAMERA + 0x64, 0xC23B3333)
    for address in CAMERA_CALLS:
        def boundary(o, address=address):
            events.append((address, o.load(LOCK, 1)))
            o.f[0] = 0
        original.calls[address] = boundary
    return original


def frame_order(elf, entry, top, release):
    events = []
    original = camera_oracle(elf, 17, 1, top, events)
    original.save(0x275B44, CAMERA)
    boundaries = (0x1CB590, 0x15BCF0, 0x1CB5A0, 0x1D1C50,
                  0x1C1D00, 0x1AFD70, 0x15C160, 0x1F0360,
                  0x1AAD00, 0x1D1EA0)
    for address in boundaries:
        def boundary(o, address=address):
            argument = o.r[4] & 0xFFFFFFFF
            events.append((address, o.load(LOCK, 1), argument))
            if address == 0x1CB590:
                o.save(0x275B44, argument)
            if release and address == 0x1AFD70 and argument == (0 if entry == 0x1AE5E0 else 2):
                # The owner's verified frame-sub4 write occurs before camera.
                o.save(LOCK, 80, 1)
        original.calls[address] = boundary
    original.run(entry)
    player = [event for event in events if event[0] == 0x15BCF0]
    camera = [event for event in events if event[0] == 0x18C0D0]
    assert len(player) == len(camera) == 1
    assert player[0][1] == 17 and camera[0][1] == (79 if release else 16)
    assert events.index(player[0]) < events.index(camera[0])
    return original.load(LOCK, 1)


def main():
    elf = (ROOT.parent / 'Extermination/config/SCUS_971.12').read_bytes()
    assert hashlib.sha256(elf).hexdigest() == ELF_SHA
    report = {}
    with tempfile.TemporaryDirectory(prefix='interaction_recovery_') as temporary:
        library = Path(temporary) / 'recovery.dylib'
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-ffp-contract=off', '-fPIC', '-shared', '-Isrc',
                        'src/game/em_interaction_scan.c', '-lm', '-o', str(library)],
                       cwd=ROOT, check=True)
        native = C.CDLL(str(library))
        native.em_interaction_scan.argtypes = [C.POINTER(State), C.POINTER(Candidate),
            C.c_size_t, PREDICATE, C.c_void_p, C.POINTER(C.c_size_t)]

        count = 0
        for remaining, state, top in itertools.product(range(256), (0, 1, 2, 255), (0, 1, 2, 3, 255)):
            events = []
            original = camera_oracle(elf, remaining, state, top, events)
            original.run(0x18B9C0, (CAMERA,))
            actual = max(remaining - 1, 0)
            assert actual == original.load(LOCK, 1), (remaining, state, top, actual)
            assert all(event[1] == actual for event in events)
            count += 1
        report['camera_state_top_mode_cases'] = count

        count = 0
        for entry, top, release in itertools.product((0x1AE5E0, 0x1AE6B0), (0, 1, 2, 3), (False, True)):
            expected = frame_order(elf, entry, top, release)
            assert expected == (79 if release else 16)
            count += 1
        report['original_frame_order_cases'] = count

        # Use reads the pre-camera value:1 still inhibits this frame, even
        # though the later camera callback will decrement it to0.
        for remaining in range(256):
            arbitration(elf, native, [(1, 0x80, 0, 1, 5.0)], gates=(0, 0, remaining))
        report['use_inhibition_cases'] = 256

        # Status phase3 completion writes70, while phase5 commits camera
        # without executing18B9C0. The final status frame remains consumed.
        for remaining in (0, 1, 70, 80, 255):
            state = Status(3, 1, 0, 1, remaining, 0)
            completed, _ = StatusOracle(elf, state, 1).run()
            assert completed.phase == 5 and completed.recovery_lock == 70
            released, events = StatusOracle(elf, completed, 0).run()
            assert released.phase == 1 and released.recovery_lock == 70 and 11 in events
        report['status_completion_release_pairs'] = 5

    report['scope'] = 'original scheduling contract plus native Use gate; rendering/geometry boundaries'
    output = ROOT / 'build/interaction_recovery_reference'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('interaction recovery original reference PASS:', json.dumps(report))


if __name__ == '__main__':
    main()
