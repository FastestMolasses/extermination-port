#!/usr/bin/env python3
"""Compare the first-control native player record with the original's.

The native side is the New Game control fixture with the run-stop
interruption (EM_STARTUP_TEST=newgame-control EM_CONTROL_REENTRY_TEST=1
EM_CONTROL_TRACE=1): 30 held input ticks, 18 released, 8 held again. Since
census L12 the idle / walk states 00161020 / 001612D0 own the player, so the
fixture prints the whole player record after every player stage. The
original side is the recorded actor bytes of the same input from the
immutable save state 04 (`collision_run_poll.json`, local, ignored).

Alignment uses the first accepted movement callback (the walk clip 1
requested, +20C == 1), not a guessed startup or movie duration. For each of
the 56 callbacks it compares, byte for byte:

  - the pose source: clip +20C, clock +3C, transition (+2C & 0x8000) and
    flags +200 (as before L12);
  - the locomotion state: +4, +5, +6, +7, +A, +2C, +38, +1F0, +1F1, +204,
    +208, +212, +235, +236, +23A, +23B, +23C, +23F, +240, +244, +248, +24C,
    +25C, +25E (the footstep phase), +260 / +264 / +268 (the foot-placement
    stop), +314;
  - the feet position: the native +B0..+B8 against the original +A0..+A8
    (0015BCF0 leaves the feet in +A0 and the hip in +B0; the port keeps the
    feet in +B0 between stages and publishes the hip separately);
  - the heading +C4, exactly, except on a callback whose camera heading
    input D_008106A0 (read by 00174AC0 at the previous callback) differs
    from the original's sample: that word is the live camera's (census
    L13..L16, CAMERA_LIVE.md), and there +C4 may differ by the turn's few
    ulps only;
  - the idle counter +28 as a constant offset: the save state starts some
    idle callbacks after the fade clear, the fixture presses at once.

Both input files are local ignored artifacts. Run through
`make test-first-control-reference`.
"""
import argparse
import json
from pathlib import Path
import re
import struct


ROOT = Path(__file__).resolve().parents[1]

FIELDS = (
    (0x04, 4), (0x0A, 1), (0x2C, 2), (0x38, 4), (0x3C, 4), (0x1F0, 2), (0x200, 4), (0x204, 4),
    (0x208, 4), (0x20C, 2), (0x212, 2), (0x235, 2), (0x23A, 3), (0x23F, 1), (0x240, 4),
    (0x244, 4), (0x248, 4), (0x24C, 4), (0x25C, 1), (0x25E, 1), (0x260, 4), (0x264, 4),
    (0x268, 4), (0x314, 1),
)
HEADING_ULPS = 16


def u32(buf, at):
    return struct.unpack_from('<I', buf, at)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, default=ROOT / "build/player_pose_channels/live_reentry.log")
    parser.add_argument("--original", type=Path,
                        default=ROOT.parent / "Extermination/build/startup-reference/collision_run_poll.json")
    parser.add_argument("--output", type=Path,
                        default=ROOT / "build/player_pose_channels/live_source_proof.json")
    args = parser.parse_args()
    log = args.native.read_text(errors="replace")
    assert "newgame reentry test: PASS" in log, "native input regression did not finish"
    first = int(re.search(r"control sample: tick=1 frame=(\d+)", log).group(1))
    poses = {
        int(frame): (int(clip), struct.pack("<f", float(time)), int(blend), int(flags, 16))
        for frame, clip, time, blend, flags in re.findall(
            r"pose sample: frame=(\d+) clip=(\d+) remaining=(\S+) transition=(\d) flags=([a-f0-9]+)", log)
    }
    records = {int(frame): (int(yaw, 16), bytes.fromhex(hexed)) for frame, yaw, hexed in re.findall(
        r"record sample: frame=(\d+) camera_yaw=([0-9a-f]+) hex=([0-9a-f]+)", log)}
    capture = json.loads(args.original.read_text())
    rows = {row["frame"]: row for row in capture["rows"]}
    actors = {frame: bytes.fromhex(row["actor_hex"]) for frame, row in rows.items()}
    original_first = next(frame for frame, actor in actors.items()
                          if struct.unpack_from("<H", actor, 0x20C)[0] == 1)
    matches = []
    counter_offset = None
    camera_ticks = []
    for tick in range(56):  # movement30, run-stop18, interrupted-stop8
        actor = actors[original_first + tick]
        expected = (struct.unpack_from("<H", actor, 0x20C)[0], actor[0x3C:0x40],
                    int(bool(struct.unpack_from("<H", actor, 0x2C)[0] & 0x8000)),
                    struct.unpack_from("<I", actor, 0x200)[0])
        actual = poses[first + tick]
        assert actual == expected, (tick + 1, actual, expected)
        camera, native = records[first + tick]
        for at, size in FIELDS:
            assert native[at:at + size] == actor[at:at + size], \
                ('record', tick + 1, hex(at), native[at:at + size].hex(), actor[at:at + size].hex())
        assert native[0xB0:0xBC] == actor[0xA0:0xAC], \
            ('feet', tick + 1, native[0xB0:0xBC].hex(), actor[0xA0:0xAC].hex())
        offset = struct.unpack_from('<H', native, 0x28)[0] - struct.unpack_from('<H', actor, 0x28)[0]
        counter_offset = offset if counter_offset is None else counter_offset
        assert offset == counter_offset, ('+28 offset', tick + 1, offset, counter_offset)
        heading, want = u32(native, 0xC4), u32(actor, 0xC4)
        if heading != want:
            previous_camera = records[first + tick - 1][0]
            original_camera = struct.unpack('<I', struct.pack(
                '<f', rows[original_first + tick - 1]["camera_yaw"]))[0]
            assert previous_camera != original_camera, \
                ('+C4 differs with the same camera heading input', tick + 1, hex(heading), hex(want))
            assert heading >> 31 == want >> 31 and abs(heading - want) <= HEADING_ULPS, \
                ('+C4 beyond the camera input difference', tick + 1, hex(heading), hex(want))
            camera_ticks.append(tick + 1)
        matches.append({"native_frame": first + tick, "original_frame": original_first + tick,
                        "clip": actual[0], "remaining_bits": actual[1].hex(),
                        "transition": actual[2], "flags": actual[3]})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"matched_callbacks": len(matches),
                                      "alignment": "first accepted input",
                                      "heading_camera_input_ticks": camera_ticks,
                                      "idle_counter_offset": counter_offset,
                                      "rows": matches}, indent=2) + "\n")
    print("first-control reference PASS: 56 callbacks, the record (pose source, locomotion "
          "state, feet) exact against the original; +C4 differs only after the live camera's "
          "heading input did (callbacks %s)" % (camera_ticks or 'none'))


if __name__ == "__main__":
    main()
