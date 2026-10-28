#!/usr/bin/env python3
"""Capture a headless, aligned native audio comparison on macOS arm64.

Use a private binary prepared by prepare_spu2_observer.py. Its comparison
anchor hook activates the existing drive replay for ordinary input playback.
The recorded input, reference counters, WAVs and traces stay in ignored build/.
This tool never patches application sources or changes the default profile.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import time
import wave

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--recording", required=True, type=Path)
    ap.add_argument("--reference-recording", required=True, type=Path)
    ap.add_argument("--drive-replay", required=True, type=Path)
    ap.add_argument("--binary", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--observe", action="store_true")
    ap.add_argument("--timeout", type=float, default=600)
    args = ap.parse_args(argv)
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        ap.error("this capture runs on native arm64 macOS only")
    output = args.out.resolve()
    if (ROOT / "build").is_symlink() or ROOT / "build" not in output.parents:
        ap.error("output must be a real private folder under this worktree's build/")
    if output.exists():
        ap.error("output already exists; choose a fresh directory")
    binary = args.binary.resolve()
    if ROOT / "build" not in binary.parents or not binary.is_file():
        ap.error("binary must be a private build in this worktree")
    if (ROOT / "assets").is_symlink():
        ap.error("assets must be this worktree's own real directory")
    decomp = ROOT.parent / "Extermination"
    sys.path.insert(0, str(decomp / "tools/video_compare"))
    import emrec

    source = args.recording.resolve()
    reference = args.reference_recording.resolve()
    replay = args.drive_replay.resolve()
    rec = emrec.read(source)
    ref = emrec.read(reference)
    last = ref.ticks()[-1]
    end = (last["seg"], last["off"])
    rows = []
    for row in rec.rows:
        if not row["mv"] and (row["seg"], row["off"]) > end:
            break
        rows.append(row)
    if not rows or (rows[-1]["seg"], rows[-1]["off"]) != end:
        ap.error("reference endpoint must exist in the input recording")
    output.mkdir(parents=True)
    prefix = output / "input.rec"
    emrec.write(prefix, {"source": str(source), "source_sha256": digest(source)}, rows)
    env = {k: v for k, v in os.environ.items() if not k.startswith("EM_")}
    env.update(EM_HEADLESS="1", EM_UNCAPPED="1", EM_PS2_DISC_DRIVE_TIMING="1",
               EM_PS2_DRIVE_REPLAY=str(replay), EM_IOP_TICK_PHASE="68",
               EM_INPUT_PLAY=str(prefix), EM_INPUT_RECORD=str(output / "native.rec"),
               EM_REPLAY_AUDIO=str(output / "audio.wav"),
               EM_FRAME_TRACE=str(output / "ticks.jsonl"),
               EM_AREA_CHANGE_LOG=str(output / "sound-ticks.jsonl"),
               EM_REPLAY_MAX_OVERRUN="1800")
    for item in rec.header.get("env", []):
        key, _, value = item.partition("=")
        if key in ("EM_NEW_GAME", "EM_SKIP_STARTUP"):
            env[key] = value
    if args.observe:
        env["EM_SPU2_OBSERVER_DIR"] = str(output / "observer")
    receipt = {
        "schema": "spu2-native-aligned-v1", "binary": str(binary),
        "capture_tool_sha256": digest(Path(__file__)),
        "binary_sha256": digest(binary), "source_recording": str(source),
        "source_sha256": digest(source), "reference_recording": str(reference),
        "reference_sha256": digest(reference), "drive_replay": str(replay),
        "drive_replay_sha256": digest(replay), "endpoint": list(end),
        "environment": {k: v for k, v in env.items() if k.startswith("EM_")},
        "default_audio": True, "observation_enabled": args.observe,
        "application_sources": "private comparison hook; no tracked shared edits",
    }
    started = time.monotonic()
    with (output / "run.log").open("w") as log:
        proc = subprocess.Popen([str(binary)], cwd=ROOT, env=env,
                                stdout=log, stderr=subprocess.STDOUT)
        try:
            rc = proc.wait(timeout=args.timeout)
        except BaseException:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            raise
        finally:
            receipt["returncode"] = proc.returncode
            receipt["seconds"] = round(time.monotonic() - started, 3)
            receipt["binary_unchanged"] = digest(binary) == receipt["binary_sha256"]
            (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    log = (output / "run.log").read_text()
    receipt["anchor_count"] = len(re.findall(r"stream drive: replay anchor a2seg", log))
    from drive_replay import report as drive_report
    receipt["drive_replay_stats"] = drive_report(log)
    receipt["artifacts"] = {}
    for name in ("native.rec", "audio.wav", "ticks.jsonl", "sound-ticks.jsonl"):
        path = output / name
        receipt["artifacts"][name] = {"bytes": path.stat().st_size, "sha256": digest(path)}
    with wave.open(str(output / "audio.wav"), "rb") as wav:
        receipt["audio"] = {"frames": wav.getnframes(), "rate": wav.getframerate(),
                            "channels": wav.getnchannels(), "sample_bytes": wav.getsampwidth()}
    actual = emrec.read(output / "native.rec").ticks()
    receipt["ticks"] = len(actual)
    receipt["last_tick"] = {k: actual[-1][k] for k in ("counter", "seg", "off")}
    stats = receipt["drive_replay_stats"]
    receipt["status"] = "PASS" if (
        rc == 0 and receipt["anchor_count"] and stats and stats["served"] > 0
        and stats["served"] == stats["entries"]
        and stats["missed_in_span"] == 0 and stats["not_reached"] == 0
        and receipt["binary_unchanged"]
        and (actual[-1]["seg"], actual[-1]["off"]) == end
    ) else "FAIL"
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({k: receipt[k] for k in
                      ("status", "returncode", "seconds", "ticks", "anchor_count",
                       "drive_replay_stats", "audio")}))
    return 0 if receipt["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
