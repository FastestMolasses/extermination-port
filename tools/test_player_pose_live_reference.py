#!/usr/bin/env python3
"""Compare the first-control native pose trace with recorded original actor bytes.

Both input files are local ignored artifacts. Alignment uses the first accepted
movement callback, not a guessed startup or movie duration. Decimal native
floats are converted back to float32 before the remaining-clock comparison.
"""
import argparse
import json
from pathlib import Path
import re
import struct


ROOT = Path(__file__).resolve().parents[1]


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
    capture = json.loads(args.original.read_text())
    rows = {row["frame"]: bytes.fromhex(row["actor_hex"]) for row in capture["rows"]}
    original_first = next(frame for frame, actor in rows.items()
                          if struct.unpack_from("<H", actor, 0x20C)[0] == 1)
    matches = []
    for tick in range(56):  # movement30, run-stop18, interrupted-stop8
        actor = rows[original_first + tick]
        expected = (struct.unpack_from("<H", actor, 0x20C)[0], actor[0x3C:0x40],
                    int(bool(struct.unpack_from("<H", actor, 0x2C)[0] & 0x8000)),
                    struct.unpack_from("<I", actor, 0x200)[0])
        actual = poses[first + tick]
        assert actual == expected, (tick + 1, actual, expected)
        matches.append({"native_frame": first + tick, "original_frame": original_first + tick,
                        "clip": actual[0], "remaining_bits": actual[1].hex(),
                        "transition": actual[2], "flags": actual[3]})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"matched_callbacks": len(matches),
                                      "alignment": "first accepted input", "rows": matches}, indent=2) + "\n")
    print("player pose live reference PASS: 56 callbacks, exact clip/clock/transition/flags")


if __name__ == "__main__":
    main()
