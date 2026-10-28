#!/usr/bin/env python3
"""Capture the original's mixed SPU2 output through the decomp's hidden session.

Run on macOS with the decomp's .venv/bin/python. This tool consumes the existing
input recording and ForkPlayer scheduler, but starts and closes the emulator
only through pcsx2_session.ForkSession (its ownership-checked shared run lock).
No emulator implementation is imported into the port or inspected here.

All outputs are disc-derived and must remain in the port/decomp ignored build/
folders. A fresh output directory is mandatory. The audio tap is 48 kHz stereo
PCM16 after the emulated master volume, before host audio processing.

EE age-zero observations are included to locate sounds, with measured sample
positions at loop-top boundaries. They are NOT exact SPU2 key-on cycles and
are NOT isolated-voice captures.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import signal
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--recording", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--decomp", type=Path, default=ROOT.parent / "Extermination")
    parser.add_argument("--ticks", type=int, default=1800, help="post-New-Game loop ticks")
    parser.add_argument("--lock-wait", type=float, default=30)
    args = parser.parse_args(argv)
    if args.ticks <= 0 or args.lock_wait < 0:
        parser.error("ticks must be positive and lock-wait nonnegative")
    decomp = args.decomp.resolve()
    output = args.out.resolve()
    build_folders = [ROOT / "build", decomp / "build"]
    if any(folder.is_symlink() for folder in build_folders):
        parser.error("build output roots must be real directories, not symlinks")
    allowed = [folder.resolve() for folder in build_folders]
    if not any(folder in output.parents for folder in allowed):
        parser.error("output must be under the port or decomp build/ folder")
    if output.exists():
        parser.error("output already exists; choose a fresh directory")
    recording = args.recording.resolve()
    sys.path.insert(0, str(decomp / "tools"))
    sys.path.insert(0, str(decomp / "tools/video_compare"))
    import emrec
    import ps2_fork
    from pcsx2_session import ForkSession

    rec = emrec.read(recording)
    output.mkdir(parents=True)
    player = ps2_fork.ForkPlayer(rec, output, 1000000, audio=True,
                                max_ticks=args.ticks, mtvu=False)
    # Keep only audio, timing and key-on observation metadata: no field images.
    player.arm = lambda cap: None
    player.capture_buffer = lambda cap, tick_row: None
    observations = []
    original_audio_samples = player.audio_samples

    def audio_samples():
        position = original_audio_samples()
        voices = player.rd(0x27CCC0, 48 * 0x6A)
        for voice in range(48):
            at = voice * 0x6A
            kind, age = struct.unpack_from("<HH", voices, at + 0x1A)
            if kind == 2 and age == 0:
                def word(offset):
                    return struct.unpack_from("<H", voices, at + offset)[0]
                observations.append({
                    "voice": voice, "serial": word(0x0A), "note": word(2),
                    "owner": word(6), "program": word(0x3E), "bank_handle": word(0x22),
                    "row": len(player.rows), "tap_sample": position,
                    "alignment": "EE age-zero at loop top; not SPU2 key-on cycle"})
        return position

    player.audio_samples = audio_samples
    session = ForkSession(None, log_dir=output / "session", align=False, mtvu=False,
                          label="port-spu2-output-capture", lock_wait=args.lock_wait)

    def on_term(signum, frame):
        raise KeyboardInterrupt(f"signal {signum}")

    previous_handler = signal.signal(signal.SIGTERM, on_term)
    try:
        with session as original:
            player.c = original._client
            hello = player.c.call("hello")
            player.info["server_version"] = hello.get("server_version")
            player.c.call("probe_add", pc=player.post_pc, action="stop", label="audio replay pad")
            player.c.call("probe_add", pc=ps2_fork.MOVIE_DRIVER, action="stop", label="audio replay movie")
            player._drive(original._inst)
            player.info["audio_stop"] = player.c.call("audio_tap", action="stop")
            player._collect_audio(original._inst)
    finally:
        signal.signal(signal.SIGTERM, previous_handler)
        player.pool.shutdown()
        player.info["session_closed"] = session._fs is None and not session._locked
        # _drive samples its terminal boundary before deciding to stop. It has
        # no corresponding playback row, so do not present it as a row's event.
        observations = [item for item in observations if item["row"] < len(player.rows)]
        emrec.write(output / "ps2.rec",
                    {"source": "pcsx2-fork", "audio": "fork-tap", "recording": str(recording)},
                    player.rows)
        receipt = {"recording": str(recording),
                   "recording_sha256": hashlib.sha256(recording.read_bytes()).hexdigest(),
                   "info": player.info, "notes": player.notes,
                   "key_on_observations": observations,
                   "isolation": "full mixed output; no individual voice isolation"}
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"rows": len(player.rows), "ee_key_on_observations": len(observations),
                      "session_closed": player.info["session_closed"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
