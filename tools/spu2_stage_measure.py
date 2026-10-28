#!/usr/bin/env python3
"""Measure cumulative stage WAVs using recorded clocks, never waveform fitting.

Consumes the alignment report's fixed recording segment/offset spans and its
mixed sound windows. The observer-to-WAV gap must be verified independently by
the clock-origin receipt. Baseline reconstruction is checked against the actual
native WAV before interpreting the stage differences. Errors are diagnostics;
alignment uncertainty and missing original per-voice isolation prohibit PASS.
Voice stages only: there is no core routing, master or core effect stage
(docs/SPU2_OUTPUT.md "Sources and clean-room boundary").
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from spu2_stage_compare import digest, integer, private_destination
from test_spu2_output_compare import compare_spans, evidence, wav_info


def measure(stages_path, alignment_path, clock_path):
    stages = json.loads(stages_path.read_text())
    alignment = json.loads(alignment_path.read_text())
    clock = json.loads(clock_path.read_text())
    if clock.get("schema") != "spu2-observer-wav-origin-v1":
        raise ValueError("verified observer-to-WAV clock receipt required")
    gap = integer(clock["drain_to_native_wav_offset"], "drain/WAV offset")
    if (clock["mix_ranges_checked"] < 1 or
            clock["waveform_validation"]["used_to_estimate_offset"]):
        raise ValueError("clock origin must be established without waveform fitting")
    reference = Path(alignment["reference"]["path"])
    baseline = Path(alignment["native"]["path"])
    reference_info, baseline_info = wav_info(reference), wav_info(baseline)
    if reference_info["sha256"] != alignment["reference"]["sha256"]:
        raise ValueError("reference WAV differs from alignment report")
    if baseline_info["sha256"] != alignment["native"]["sha256"]:
        raise ValueError("baseline WAV differs from alignment report")
    if baseline_info["sha256"] != clock["sha256"]["wav"]:
        raise ValueError("baseline WAV differs from origin receipt")
    if stages["input_sha256"]["events"] != clock["sha256"]["events"]:
        raise ValueError("stage event timeline differs from origin receipt")
    if baseline_info["frames"] != clock["native_wav_frames"]:
        raise ValueError("baseline clock length disagrees")
    paths = {name: Path(meta["path"]) for name, meta in stages["mixed_outputs"].items()}
    infos = {name: wav_info(path) for name, path in paths.items()}
    expected_outputs = dict(stages["mixed_outputs"])
    if any(meta["sha256"] != expected_outputs[name]["sha256"] for name, meta in infos.items()):
        raise ValueError("stage WAV differs from its generating report")
    frames = stages["mix_mapping"]["frames"]
    if clock["producer_drain_frames"] != [frames, frames]:
        raise ValueError("drain lengths differ from origin receipt")
    if any(info["frames"] != frames for info in infos.values()):
        raise ValueError("stage WAV lengths differ")

    def span(item, shift):
        return {"reference_start": integer(item["reference_start"], "reference_start"),
                "native_start": integer(item["native_start"], "native_start") - shift,
                "frames": integer(item["frames"], "frames", 1)}

    paired = [span(item, 0) for item in alignment["segments"]]
    shifted = [span(item, gap) for item in alignment["segments"]]
    # Output rows retain the original fixed offsets and shifted drain offsets.
    stats = {"recorded_baseline": compare_spans(reference, baseline, paired,
                                               reference_info["frames"], baseline_info["frames"])}
    for name, path in paths.items():
        stats[name] = compare_spans(reference, path, shifted,
                                   reference_info["frames"], infos[name]["frames"])
    reconstruction_spans = [{"reference_start": x["native_start"],
                             "native_start": x["native_start"] - gap,
                             "frames": x["frames"]} for x in paired]
    reconstruction = compare_spans(baseline, paths["native_voice_sum"], reconstruction_spans,
                                   baseline_info["frames"], frames)
    windows = []
    for group in ("sound_windows", "stereo_sound_candidates", "stereo_sound_windows"):
        for item in alignment.get(group, []):
            proof = evidence(item["alignment_evidence"], item["name"])
            original_span, shifted_span = span(item, 0), span(item, gap)
            values = {"recorded_baseline": compare_spans(reference, baseline, [original_span],
                                                        reference_info["frames"], baseline_info["frames"])}
            for name, path in paths.items():
                values[name] = compare_spans(reference, path, [shifted_span],
                                            reference_info["frames"], frames)
            lag = item.get("bounded_waveform_lag")
            fixed_lag_values = None
            if lag and lag.get("comparison_span_after"):
                lag_span = span(lag["comparison_span_after"], 0)
                lag_shifted = span(lag["comparison_span_after"], gap)
                fixed_lag_values = {
                    "recorded_baseline": compare_spans(reference, baseline, [lag_span],
                                                      reference_info["frames"], baseline_info["frames"])}
                for name, path in paths.items():
                    fixed_lag_values[name] = compare_spans(
                        reference, path, [lag_shifted], reference_info["frames"], frames)
            windows.append({"name": item["name"], "kind": group,
                            "reference_start": item["reference_start"],
                            "native_wav_start": item["native_start"],
                            "native_drain_start": shifted_span["native_start"],
                            "frames": item["frames"], "alignment_evidence": proof,
                            "isolated_original_voice": False, "stages": values,
                            "baseline_fixed_lag_frames": None if not lag else lag.get("lag_frames"),
                            "baseline_fixed_lag_stages": fixed_lag_values,
                            "lag_scope": "baseline-derived bounded acoustic lag reused unchanged for every stage; not KON evidence"})
    return {"schema": "spu2-stage-errors-v1", "fidelity_pass": False,
            "tool_sha256": digest(Path(__file__)),
            "reference": reference_info, "baseline": baseline_info,
            "native_wav_offset": gap, "paired_spans": paired,
            "shifted_drain_spans": shifted,
            "whole_paired_mix": stats, "baseline_reconstruction": reconstruction,
            "windows": windows, "outputs": infos,
            "provenance": {"voice_report": digest(stages_path),
                           "alignment": digest(alignment_path), "clock": digest(clock_path)},
            "limits": alignment["limits"] + stages["limitations"] + [
                "Only the supplied corresponding spans are compared; title/load tails are excluded.",
                "All original comparison samples are mixed; no isolated original voice is available.",
                "The 800-frame-style origin correction is recorded-clock evidence, never an acoustic fit.",
                "Max/RMS measure this model experiment and do not certify Gaussian/ADSR/reverb exactness.",
            ]}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--stages", type=Path, required=True)
    p.add_argument("--alignment", type=Path, required=True)
    p.add_argument("--clock", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    args = p.parse_args()
    try:
        out = private_destination(args.out)
        out.mkdir(parents=True, exist_ok=False)
        report = measure(args.stages, args.alignment, args.clock)
        (out / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        for name, stats in report["whole_paired_mix"].items():
            print(f"{name}: max={stats['max_abs']} RMS={stats['rms']:.9f}")
        print("Mixed-reference stage errors measured; fidelity_pass=false.")
        return 0
    except (ValueError, KeyError, TypeError, OSError) as error:
        print("SPU2 stage measurement invalid: " + str(error))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
