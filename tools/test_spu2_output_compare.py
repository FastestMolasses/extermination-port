#!/usr/bin/env python3
"""Opt-in local PCM16/48 kHz SPU2 output comparison; no audio is printed.

The JSON manifest has ``reference`` and ``native`` WAV paths (relative to the
manifest), and optional ``events``. Each event names ``reference_key_on``,
``native_key_on``, ``frames``, optional ``pre_frames``, and
``alignment_evidence``: {"source": "...", "uncertainty_frames": 0}.
Positions and lengths count stereo frames, not interleaved samples. Use the
measured key-on positions: this tool does not fit gains, resample, or search
for a waveform alignment.

An optional ``mix_spans`` list supplies {reference_start, native_start, frames,
alignment_evidence} for corresponding timeline stretches. The default compares
the entire common length from sample zero. Uncovered frames are always reported
and make the exactness gate fail; a shorter matching prefix cannot pass.

Events use the mixed WAVs by default and are explicitly labelled mixed-event
windows. Per-voice claims require separate ``reference`` and ``native`` WAVs
on the event plus ``isolated_voice_evidence`` containing ``voice``, ``source``,
``reference_active_voices`` = 1 and ``native_active_voices`` = 1. This declares
the capture's isolation provenance; this tool cannot infer it from a mix.
The capture must establish that reverb history and other inputs are isolated.

Example (keep the manifest and every WAV in ignored build/):
  python3 tools/test_spu2_output_compare.py --manifest build/spu2/compare.json \
      --report build/spu2/metrics.json

Exit 0 means every supplied comparison is bit-exact, the whole mix is fully
covered, and all alignment evidence has zero uncertainty. Exit 1 is a measured
difference or incomplete evidence; exit 2 is invalid input. Only metadata,
hashes and error statistics are written to the report.
"""
from __future__ import annotations

import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import sys
import wave

RATE = 48000
CHANNELS = 2
CHUNK = 32768


def integer(value, name, minimum=0):
    if type(value) is not int or value < minimum:
        raise ValueError(f"{name} must be an integer >= {minimum}")
    return value


def wav_info(path):
    with wave.open(str(path), "rb") as wav:
        if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth(),
                wav.getcomptype()) != (RATE, CHANNELS, 2, "NONE"):
            raise ValueError(f"{path}: expected uncompressed stereo PCM16 at 48000 Hz")
        frames = wav.getnframes()
    if not frames:
        raise ValueError(f"{path}: empty audio is not evidence")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while data := stream.read(1 << 20):
            digest.update(data)
    return {"path": str(path), "frames": frames, "sha256": digest.hexdigest(),
            "rate": RATE, "channels": CHANNELS, "bits": 16}


def evidence(obj, name):
    if not isinstance(obj, dict) or not isinstance(obj.get("source"), str) or not obj["source"].strip():
        raise ValueError(f"{name}: nonempty alignment_evidence.source is required")
    integer(obj.get("uncertainty_frames"), f"{name}: uncertainty_frames")
    return obj


def compare_spans(reference, native, spans, reference_frames, native_frames):
    """Integer arithmetic until final RMS; chunking keeps full captures bounded."""
    totals = [{"sum_sq": 0, "max_abs": 0, "different_samples": 0} for _ in range(CHANNELS)]
    frame_count = 0
    first_difference = None
    with wave.open(str(reference), "rb") as ref, wave.open(str(native), "rb") as nat:
        for span_index, span in enumerate(spans):
            rs = integer(span["reference_start"], "reference_start")
            ns = integer(span["native_start"], "native_start")
            count = integer(span["frames"], "frames", 1)
            if rs + count > reference_frames or ns + count > native_frames:
                raise ValueError("comparison window exceeds a WAV; truncation is forbidden")
            ref.setpos(rs)
            nat.setpos(ns)
            done = 0
            while done < count:
                take = min(CHUNK, count - done)
                ra, na = array("h"), array("h")
                ra.frombytes(ref.readframes(take))
                na.frombytes(nat.readframes(take))
                if len(ra) != take * CHANNELS or len(na) != take * CHANNELS:
                    raise ValueError("truncated PCM data")
                if sys.byteorder != "little":
                    ra.byteswap()
                    na.byteswap()
                for channel in range(CHANNELS):
                    stats = totals[channel]
                    sum_sq = maximum = different = 0
                    for index in range(channel, len(ra), CHANNELS):
                        delta = int(na[index]) - int(ra[index])
                        absolute = abs(delta)
                        sum_sq += delta * delta
                        maximum = max(maximum, absolute)
                        different += delta != 0
                        if delta:
                            offset = done + index // CHANNELS
                            candidate = {"span": span_index, "channel": channel,
                                         "reference_frame": rs + offset,
                                         "native_frame": ns + offset}
                            if first_difference is None or (
                                    candidate["span"], candidate["reference_frame"], channel) < (
                                    first_difference["span"], first_difference["reference_frame"],
                                    first_difference["channel"]):
                                first_difference = candidate
                    stats["sum_sq"] += sum_sq
                    stats["max_abs"] = max(stats["max_abs"], maximum)
                    stats["different_samples"] += different
                done += take
            frame_count += count
    if frame_count == 0:
        raise ValueError("at least one nonempty comparison span is required")
    per_channel = [{"max_abs": t["max_abs"], "rms": math.sqrt(t["sum_sq"] / frame_count),
                    "different_samples": t["different_samples"]} for t in totals]
    maximum = max(t["max_abs"] for t in totals)
    return {"frames": frame_count, "samples": frame_count * CHANNELS,
            "max_abs": maximum,
            "rms": math.sqrt(sum(t["sum_sq"] for t in totals) / (frame_count * CHANNELS)),
            "different_samples": sum(t["different_samples"] for t in totals),
            "per_channel": per_channel, "first_difference": first_difference,
            "bit_exact": maximum == 0, "units": "signed PCM16 integer units"}


def coverage(spans, key, total):
    intervals = sorted((span[key], span[key] + span["frames"]) for span in spans)
    end = covered = 0
    for start, finish in intervals:
        if start < end:
            raise ValueError("whole-mix spans overlap; counting samples twice is forbidden")
        covered += finish - start
        end = finish
    return {"total_frames": total, "compared_frames": covered,
            "uncompared_frames": total - covered, "complete": total == covered}


def compare_manifest(manifest, base):
    if not isinstance(manifest, dict):
        raise ValueError("manifest must be a JSON object")

    def path_for(value):
        if not isinstance(value, str) or not value:
            raise ValueError("a WAV path must be a nonempty string")
        return (base / value).resolve()

    reference, native = path_for(manifest["reference"]), path_for(manifest["native"])
    ri, ni = wav_info(reference), wav_info(native)
    spans = manifest.get("mix_spans")
    if spans is None:
        spans = [{"reference_start": 0, "native_start": 0,
                  "frames": min(ri["frames"], ni["frames"]),
                  "alignment_evidence": manifest.get("alignment_evidence")}]
    if not isinstance(spans, list) or not spans:
        raise ValueError("mix_spans must be a nonempty list")
    uncertain = False
    for span in spans:
        if not isinstance(span, dict):
            raise ValueError("each mix_spans entry must be a JSON object")
        proof = evidence(span.get("alignment_evidence"), "mix")
        uncertain |= proof["uncertainty_frames"] != 0
    mix = compare_spans(reference, native, spans, ri["frames"], ni["frames"])
    mix["reference_coverage"] = coverage(spans, "reference_start", ri["frames"])
    mix["native_coverage"] = coverage(spans, "native_start", ni["frames"])
    mix["alignment_uncertain"] = uncertain
    mix["alignment_spans"] = spans
    all_exact = (mix["bit_exact"] and not uncertain and
                 mix["reference_coverage"]["complete"] and mix["native_coverage"]["complete"])
    events = manifest.get("events", [])
    if not isinstance(events, list):
        raise ValueError("events must be a list")
    results, names = [], set()
    for event in events:
        if not isinstance(event, dict):
            raise ValueError("each events entry must be a JSON object")
        name = event.get("name")
        if not isinstance(name, str) or not name or name in names:
            raise ValueError("events require unique nonempty names")
        names.add(name)
        proof = evidence(event.get("alignment_evidence"), name)
        pre = integer(event.get("pre_frames", 0), "pre_frames")
        rk = integer(event["reference_key_on"], "reference_key_on")
        nk = integer(event["native_key_on"], "native_key_on")
        frames = integer(event["frames"], "frames", 1)
        ref_path = path_for(event["reference"]) if "reference" in event else reference
        nat_path = path_for(event["native"]) if "native" in event else native
        eri = ri if ref_path == reference else wav_info(ref_path)
        eni = ni if nat_path == native else wav_info(nat_path)
        isolation = event.get("isolated_voice_evidence")
        kind = "mixed_event_window"
        if isolation is not None:
            if (not isinstance(isolation, dict) or
                    type(isolation.get("reference_active_voices")) is not int or
                    type(isolation.get("native_active_voices")) is not int or
                    isolation.get("reference_active_voices") != 1 or
                    isolation.get("native_active_voices") != 1 or
                    not str(isolation.get("voice", "")).strip() or
                    not isinstance(isolation.get("source"), str) or
                    not isolation["source"].strip() or
                    ref_path == reference or nat_path == native):
                raise ValueError(f"{name}: per-voice evidence requires separate isolated captures and provenance")
            kind = "isolated_voice_capture"
        stats = compare_spans(ref_path, nat_path,
                              [{"reference_start": rk - pre, "native_start": nk - pre, "frames": frames}],
                              eri["frames"], eni["frames"])
        stats.update(name=name, kind=kind, reference_key_on=rk, native_key_on=nk,
                     alignment_evidence=proof, isolated_voice_evidence=isolation,
                     reference=eri, native=eni)
        all_exact &= stats["bit_exact"] and proof["uncertainty_frames"] == 0
        results.append(stats)
    return {"reference": ri, "native": ni, "whole_mix": mix, "events": results,
            "isolated_voice_count": sum(r["kind"] == "isolated_voice_capture" for r in results),
            "mixed_event_count": sum(r["kind"] == "mixed_event_window" for r in results),
            "all_bit_exact_with_complete_alignment": bool(all_exact),
            "claim_scope": "Only the supplied local captures and windows, relative to the reference WAV."}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--report", type=Path, help="metadata and error statistics only; no PCM")
    args = parser.parse_args(argv)
    try:
        report = compare_manifest(json.loads(args.manifest.read_text()), args.manifest.resolve().parent)
        if args.report:
            inputs = {args.manifest.resolve(), Path(report["reference"]["path"]),
                      Path(report["native"]["path"])}
            for event in report["events"]:
                inputs.update((Path(event["reference"]["path"]), Path(event["native"]["path"])))
            if args.report.resolve() in inputs:
                raise ValueError("report path aliases an input; refusing to overwrite it")
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        mix = report["whole_mix"]
        result = "PASS" if report["all_bit_exact_with_complete_alignment"] else "DIFF"
        print(f"SPU2 output {result}: whole mix max={mix['max_abs']} RMS={mix['rms']:.9f}; "
              f"{report['mixed_event_count']} mixed-event windows, "
              f"{report['isolated_voice_count']} isolated-voice captures")
        for event in report["events"]:
            print(f"  {event['name']}: {event['kind']} max={event['max_abs']} RMS={event['rms']:.9f}")
        print(f"  uncompared frames: reference={mix['reference_coverage']['uncompared_frames']} "
              f"native={mix['native_coverage']['uncompared_frames']}; "
              f"mix alignment uncertain={mix['alignment_uncertain']}")
        return 0 if result == "PASS" else 1
    except (OSError, ValueError, KeyError, TypeError, EOFError, wave.Error) as exc:
        print(f"SPU2 output invalid input: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
