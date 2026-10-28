#!/usr/bin/env python3
"""Validate reconstructed native voices against the captured native WAV.

Run with the decomp virtualenv (NumPy) on native macOS arm64. A previously
verified counter-derived clock receipt supplies the origin; this tool never
fits a waveform shift. Startup/movie contributors are absent from the voice
reconstruction, so discrepancies there remain visible. This compares native
capture and reconstruction, not original hardware fidelity.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
import wave

import numpy as np

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as source:
        while data := source.read(1 << 20):
            value.update(data)
    return value.hexdigest()


def pcm(path):
    with wave.open(str(path), "rb") as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 2, 48000)
        return np.frombuffer(wav.readframes(wav.getnframes()), dtype="<i2").astype(np.int32).reshape(-1, 2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--reconstruction", type=Path, required=True)
    parser.add_argument("--clock", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if not out.is_relative_to(ROOT / "build") or out.exists():
        parser.error("choose a fresh private JSON path under build/")
    clock = json.loads(args.clock.read_text())
    assert clock["schema"] == "spu2-observer-wav-origin-v1"
    recording, baseline = args.baseline / "native.rec", args.baseline / "audio.wav"
    assert digest(recording) == clock["sha256"]["recording"]
    assert digest(baseline) == clock["sha256"]["wav"]
    assert clock["waveform_validation"]["used_to_estimate_offset"] is False
    origin = clock["drain_to_native_wav_offset"]
    a, b = pcm(baseline), pcm(args.reconstruction)
    assert origin >= 0 and a[origin:].shape == b.shape
    b = np.concatenate((np.zeros((origin, 2), dtype=np.int32), b))
    sys.path.insert(0, str(ROOT.parent / "Extermination/tools/video_compare"))
    import emrec
    ticks = emrec.read(recording).ticks()
    segments = []
    for segment in sorted({row["seg"] for row in ticks}):
        indices = [i for i, row in enumerate(ticks) if row["seg"] == segment]
        first, last = indices[0], indices[-1]
        start = ticks[first]["af"]
        end = ticks[last + 1]["af"] if last + 1 < len(ticks) else len(a)
        error = a[start:end] - b[start:end]
        segments.append({
            "segment": segment, "first_counter": ticks[first]["counter"],
            "last_counter": ticks[last]["counter"], "first_af": start, "end_af": end,
            "frames": end - start,
            "max_absolute_error": int(np.max(np.abs(error))) if len(error) else 0,
            "nonidentical_frames": int(np.any(error != 0, axis=1).sum()),
            "frames_exceeding_1_lsb": int(np.any(np.abs(error) > 1, axis=1).sum()),
            "rms_error": float(np.sqrt(np.mean(error.astype(np.float64) ** 2))) if len(error) else 0,
        })
    report = {
        "schema": "spu2-observer-reconstruction-check-v1",
        "baseline": str(baseline.resolve()), "reconstruction": str(args.reconstruction.resolve()),
        "clock_origin_receipt": str(args.clock.resolve()), "frames": len(a),
        "segments": segments,
        "sha256": {"baseline": digest(baseline), "recording": digest(recording),
                   "reconstruction": digest(args.reconstruction), "clock": digest(args.clock),
                   "tool": digest(Path(__file__))},
        "scope": "SFX and stream only. Applied counter-derived origin, not waveform fitting. "
                 "Larger errors may include omitted startup/movie producers. Equality bounds "
                 "the net omitted contribution and arithmetic difference, not each producer independently.",
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2) + "\n")
    exact = [row for row in segments if not row["nonidentical_frames"]]
    print(json.dumps({"receipt": str(out), "exact_segments": [row["segment"] for row in exact],
                      "exact_frames": sum(row["frames"] for row in exact),
                      "comparison": "native reconstruction, not original fidelity"}))


if __name__ == "__main__":
    main()
