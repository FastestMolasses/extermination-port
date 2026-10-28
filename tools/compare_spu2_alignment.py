#!/usr/bin/env python3
"""Compare aligned original/native audio captures without fitting or resampling.

Run with the decomp virtualenv (numpy). Inputs and the NEW output directory
must remain in this worktree's ignored build/. Segment/offset pairs determine
the unshifted comparison; unmatched load tails and native title/movie audio are
reported as uncovered. Acoustic lag searches are diagnostics bounded to 801
samples (one 59.94 Hz tick), never evidence of exact SPU2 KON or isolation.
Exit 0 means the diagnostic completed, not that native audio is faithful.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
from pathlib import Path
import sys
import wave

import numpy as np

from test_spu2_output_compare import compare_spans, coverage, wav_info

ROOT = Path(__file__).resolve().parents[1]
LAG = 801
RATE = 48000
VOICE_NAMES = ("state", "note", "owner", "release", "serial", "sustain",
               "kind", "age", "priority", "alloc", "base", "depth",
               "remaining", "length", "pan", "program", "bank_handle")


def load_jsonl(path):
    rows = []
    for line in path.read_text().splitlines():
        item = json.loads(line)
        if not isinstance(item, dict):
            raise ValueError(f"{path}: expected JSON objects")
        rows.append(item)
    return rows


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1 << 20):
            value.update(chunk)
    return value.hexdigest()


def load_pcm(path):
    info = wav_info(path)
    with wave.open(str(path), "rb") as w:
        data = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2)
    if len(data) != info["frames"]:
        raise ValueError("truncated WAV")
    return info, data


def unique_map(rows):
    result = {}
    for row in rows:
        key = row["seg"], row["off"]
        if min(key) < 0:
            continue
        if key in result:
            raise ValueError(f"duplicate playback segment/offset {key}")
        result[key] = row
    return result


def with_ends(rows, total):
    for i, row in enumerate(rows):
        row["end_af"] = rows[i + 1]["af"] if i + 1 < len(rows) else total
        if not 0 <= row["af"] <= row["end_af"] <= total:
            raise ValueError("nonmonotonic or out-of-range audio positions")
    return rows


def paired_spans(ref_rows, nat_rows):
    rm, nm = unique_map(ref_rows), unique_map(nat_rows)
    keys = sorted(rm.keys() & nm.keys())
    groups = []
    for key in keys:
        if not groups or key[0] != groups[-1][-1][0] or key[1] != groups[-1][-1][1] + 1:
            groups.append([])
        groups[-1].append(key)
    spans = []
    for group in groups:
        first, last = group[0], group[-1]
        r0, n0, r1, n1 = rm[first], nm[first], rm[last], nm[last]
        count = min(r1["end_af"] - r0["af"], n1["end_af"] - n0["af"])
        if count > 0:
            spans.append({"segment": first[0], "first_offset": first[1],
                          "last_offset": last[1], "paired_rows": len(group),
                          "reference_start": r0["af"], "native_start": n0["af"],
                          "frames": count,
                          "reference_available_frames": r1["end_af"] - r0["af"],
                          "native_available_frames": n1["end_af"] - n0["af"]})
    if not spans:
        raise ValueError("no corresponding nonempty playback segments")
    return spans, rm, nm


def best_wave_lag(reference, native, bound):
    """SSE minimum over a fixed reference interior, via exact-shape FFT dots."""
    length = min(len(reference), len(native))
    if length <= 2 * bound + 48:
        return None
    a = reference[bound:length - bound].astype(np.float64)
    b = native[:length].astype(np.float64)
    size = 1 << (len(a) + len(b) - 1).bit_length()
    dots = np.zeros(2 * bound + 1)
    for channel in range(2):
        dots += np.fft.irfft(np.fft.rfft(b[:, channel], size) *
                            np.conj(np.fft.rfft(a[:, channel], size)), size)[:len(dots)]
    prefix = np.concatenate(([0.0], np.cumsum(np.sum(b * b, axis=1))))
    squared = np.sum(a * a) + prefix[len(a):len(a) + len(dots)] - prefix[:len(dots)] - 2 * dots
    # A tie chooses the smallest displacement, then the earlier native sample.
    minimum = np.min(squared)
    ties = np.flatnonzero(np.isclose(squared, minimum, rtol=1e-12, atol=1e-6))
    lag = min((int(x) - bound for x in ties), key=lambda x: (abs(x), x))
    return {"lag_frames": lag, "search_bound_frames": bound,
            "at_search_boundary": abs(lag) == bound, "interior_offset": bound,
            "interior_frames": len(a), "sign": "positive means native is later"}


def envelope_diagnostic(reference, native, bound):
    length = min(len(reference), len(native))
    count = (length - 2 * bound) // 48
    if count < 2:
        return None
    a = reference[bound:bound + count * 48].astype(np.float64)
    env_a = np.sqrt(np.mean(a.reshape(count, 48, 2) ** 2, axis=(1, 2)))
    scores = []
    for lag in range(-(bound // 48) * 48, bound + 1, 48):
        b = native[bound + lag:bound + lag + count * 48].astype(np.float64)
        env_b = np.sqrt(np.mean(b.reshape(count, 48, 2) ** 2, axis=(1, 2)))
        scores.append((float(np.sqrt(np.mean((env_a - env_b) ** 2))), lag))
    error, lag = min(scores, key=lambda x: (x[0], abs(x[1]), x[1]))

    def onset(data):
        take = len(data) // 48
        if not take:
            return None
        env = np.sqrt(np.mean(data[:take * 48].astype(np.float64).reshape(take, 48, 2) ** 2,
                              axis=(1, 2)))
        hits = np.flatnonzero(env >= 32)
        return int(hits[0]) * 48 if len(hits) else None

    ra, na = onset(reference), onset(native)
    return {"lag_frames": lag, "bin_frames": 48, "search_bound_frames": bound,
            "envelope_rms_error": error, "onset_threshold_pcm16_rms": 32,
            "reference_onset_offset": ra, "native_onset_offset": na,
            "onset_delta_frames": na - ra if ra is not None and na is not None else None,
            "note": "1ms RMS bins; no amplitude normalization; onset is not a key-on"}


def metrics(ref_path, nat_path, spans, ri, ni):
    return compare_spans(ref_path, nat_path, spans, ri["frames"], ni["frames"])


def describe_window(span, ref_path, nat_path, reference, native, ri, ni):
    rs, ns, count = (span[k] for k in ("reference_start", "native_start", "frames"))
    if rs < 0 or ns < 0 or min(count, ri["frames"] - rs, ni["frames"] - ns) != count:
        raise ValueError("window exceeds input audio")
    result = dict(span)
    result["unshifted"] = metrics(ref_path, nat_path, [span], ri, ni)
    ra, na = reference[rs:rs + count], native[ns:ns + count]
    result["envelope"] = envelope_diagnostic(ra, na, LAG)
    lag = best_wave_lag(ra, na, LAG)
    if lag:
        core = {"reference_start": rs + lag["interior_offset"],
                "native_start": ns + lag["interior_offset"],
                "frames": lag["interior_frames"]}
        before = metrics(ref_path, nat_path, [core], ri, ni)
        shifted = dict(core, native_start=core["native_start"] + lag["lag_frames"])
        lag.update(before=before, after=metrics(ref_path, nat_path, [shifted], ri, ni),
                   comparison_span_before=core, comparison_span_after=shifted,
                   claim="bounded acoustic diagnostic only; no exact KON inference")
    result["bounded_waveform_lag"] = lag
    return result


def native_sound_rows(path):
    sound = {}
    for row in load_jsonl(path):
        if "counter" not in row or "snd" not in row:
            continue
        counter = row["counter"]
        if counter in sound:
            raise ValueError("duplicate native tick counter")
        snd = row.get("snd")
        if snd is None:
            sound[counter] = None
            continue
        voices = []
        for v in snd["vo"]:
            if len(v) != 18:
                raise ValueError("native snd.vo must contain index plus 17 fields")
            voices.append({"voice": v[0], **dict(zip(VOICE_NAMES, v[1:]))})
        envx = dict(snd["envx"])
        for voice in voices:
            voice["envx_reply"] = envx.get(voice["voice"], 0)
        sound[counter] = {"voices": voices, "positive_envx": sorted(v for v, e in envx.items() if e > 0),
                          "stream": {"active": snd["lane"][:3], "mono": snd["lane"][5],
                                     "cue": snd["cue"]}, "track_ids": dict(snd["tr"])}
    return sound


def event_groups(rows):
    seen = set()
    groups = []
    for row in rows:
        owners = collections.defaultdict(list)
        for voice in row.get("voices", []):
            key = voice["voice"], voice["serial"]
            if (voice["kind"] == 2 and voice["age"] == 0 and voice["state"]
                    and voice["owner"] != 0xFFFF and key not in seen):
                seen.add(key)
                owners[voice["owner"]].append(voice)
        for owner, voices in owners.items():
            signatures = sorted((v["note"], v["program"], v["bank_handle"]) for v in voices)
            groups.append({**{k: row[k] for k in ("counter", "seg", "off", "af")},
                           "owner": owner, "voices": [v["voice"] for v in voices],
                           "serials": [v["serial"] for v in voices],
                           "signature": signatures,
                           "sound_id": row.get("track_ids", {}).get(owner),
                           "positive_envx": row.get("positive_envx", sorted(
                               v["voice"] for v in row["voices"] if v.get("envx_reply", 0) > 0))})
    return groups


def match_events(reference, native):
    used, matched, missing = set(), [], []
    for event in reference:
        possible = [(abs(event["off"] - other["off"]), i, other)
                    for i, other in enumerate(native)
                    if i not in used and event["seg"] == other["seg"]
                    and event["signature"] == other["signature"]
                    and abs(event["off"] - other["off"]) <= 2]
        if not possible:
            missing.append(event)
            continue
        delta, i, other = min(possible, key=lambda x: (x[0], x[1]))
        used.add(i)
        equally_near = sum(distance == delta for distance, _, _ in possible)
        matched.append((event, other, delta, equally_near))
    return matched, missing, [x for i, x in enumerate(native) if i not in used]


class ObserverOccupancy:
    """Map every native active-voice observation through recorded producer pulls."""

    def __init__(self, path, native_path, rec, native_info):
        self.path = path
        capture = json.loads((path / "capture.json").read_text())
        if not capture["complete"] or capture["voice_record_bytes"] != 64:
            raise ValueError("incomplete or unsupported observer capture")
        if capture["generations"] != [1, 1]:
            raise ValueError("multiple observer generations need an explicit clock map")
        for role in ("events", "voices"):
            name = capture["files"][role]
            candidate = check_path(path / name)
            if not candidate.is_relative_to(path.resolve()):
                raise ValueError("observer payload must remain inside its capture directory")
            if digest(candidate) != capture["sha256"][name]:
                raise ValueError(f"observer {role} hash differs from capture metadata")
        # The sound-log run, baseline and observer are separate runs. Never
        # borrow observer state unless the associated PCM and recording agree.
        observed_info = wav_info(path.parent / "audio.wav")
        if observed_info["sha256"] != native_info["sha256"]:
            raise ValueError("observer audio differs from the compared native WAV")
        if (path.parent / "native.rec").read_bytes() != (native_path / "native.rec").read_bytes():
            raise ValueError("observer recording differs from compared native recording")
        by_step = {r["step"]: r for r in rec.rows}
        self.ranges, differences = {0: [], 1: []}, set()
        for event in load_jsonl(path / capture["files"]["events"]):
            if event.get("type") != "mix_range":
                continue
            row = by_step.get(event["vblank"] - 1)
            if row is None or row["counter"] != event["counter"]:
                raise ValueError("observer pull cannot be paired with recording counter/vblank")
            differences.add(row["af"] - event["output_frame"])
            self.ranges[event["domain"]].append(event)
        if len(differences) != 1 or not all(self.ranges.values()):
            raise ValueError("observer does not have one verified output-to-WAV offset")
        self.offset = differences.pop()
        if any(n + self.offset != native_info["frames"] for n in capture["output_frames"]):
            raise ValueError("observer output extent differs from WAV extent")
        dtype = np.dtype({
            "names": ["frame", "domain", "voice", "envx", "gain_l", "gain_r"],
            "formats": ["<u8", "<u4", "<u4", "<u2", "<f4", "<f4"],
            "offsets": [0, 8, 12, 26, 36, 40], "itemsize": 64})
        raw = path / capture["files"]["voices"]
        if raw.stat().st_size != capture["voice_records"] * 64:
            raise ValueError("observer active-voice record count differs from metadata")
        self.data = np.memmap(raw, dtype=dtype, mode="r")
        self.provenance = {
            "capture": str(path / "capture.json"), "output_to_WAV_offset": self.offset,
            "counter_vblank_pairs_verified": sum(map(len, self.ranges.values())),
            "observer_audio_sha256": observed_info["sha256"],
            "active_voice_rows_sha256": capture["sha256"][capture["files"]["voices"]],
            "activity_rule": "legacy voice on and envelope phase not OFF; includes zero ENVX attack",
            "scope": "native legacy producer activity; no original hardware or wet-history inference",
        }

    def inspect(self, start, frames):
        end = start + frames
        result = {"native_start": start, "frames": frames, "domains": {}}
        activity = {}
        for domain in (0, 1):
            intervals, covered = [], 0
            for event in self.ranges[domain]:
                out_start = event["output_frame"] + self.offset
                low, high = max(start, out_start), min(end, out_start + event["requested"])
                if low >= high:
                    continue
                covered += high - low
                # A short producer pull contributes explicit zero-fill after count.
                high = min(high, out_start + event["count"])
                if low >= high:
                    continue
                a = event["source_frame"] + low - out_start
                b = event["source_frame"] + high - out_start
                if (intervals and intervals[-1][1] == a
                        and intervals[-1][2] + intervals[-1][1] - intervals[-1][0] == low):
                    intervals[-1][1] = b
                else:
                    intervals.append([a, b, low])
            if covered != frames:
                raise ValueError("candidate extends beyond observed native producer pulls")
            domain_mask = self.data["domain"] == domain
            found = []
            source_frames = 0
            totals = np.zeros(frames, dtype=np.int16)
            voice0, voice1 = np.zeros_like(totals), np.zeros_like(totals)
            wrong_pan = np.zeros(frames, dtype=bool)
            for low, high, output_low in intervals:
                mask = domain_mask & (self.data["frame"] >= low) & (self.data["frame"] < high)
                part = self.data[mask]
                found.append(part)
                source_frames += high - low
                indices = part["frame"].astype(np.int64) - low + output_low - start
                np.add.at(totals, indices, 1)
                np.add.at(voice0, indices[part["voice"] == 0], 1)
                np.add.at(voice1, indices[part["voice"] == 1], 1)
                wrong = ((part["voice"] == 0) & (part["gain_l"] != 0)
                         | (part["voice"] == 1) & (part["gain_r"] != 0))
                wrong_pan[indices[wrong]] = True
            activity[domain] = (totals, voice0, voice1, wrong_pan)
            rows = np.concatenate(found) if found else np.empty(0, dtype=self.data.dtype)
            voices, counts = np.unique(rows["voice"], return_counts=True)
            unique_frames, per_frame = np.unique(rows["frame"], return_counts=True)
            voice_counts = {str(int(v)): int(n) for v, n in zip(voices, counts)}
            right, left = rows[rows["voice"] == 0], rows[rows["voice"] == 1]
            result["domains"][str(domain)] = {
                "source_intervals": [x[:2] for x in intervals], "source_frames": source_frames,
                "zero_fill_frames": frames - source_frames,
                "active_voice_rows": len(rows), "voice_row_counts": voice_counts,
                "frames_with_active_voice_rows": len(unique_frames),
                "minimum_voices_per_active_frame": int(min(per_frame)) if len(per_frame) else 0,
                "maximum_voices_per_active_frame": int(max(per_frame)) if len(per_frame) else 0,
                "voice0_right_only": bool(len(right) and np.all(right["gain_l"] == 0)),
                "voice1_left_only": bool(len(left) and np.all(left["gain_r"] == 0)),
            }
        sfx, stream = result["domains"]["0"], result["domains"]["1"]
        result["stereo_pair_only"] = (
            sfx["active_voice_rows"] == 0 and stream["source_frames"] == frames
            and stream["voice_row_counts"] == {"0": frames, "1": frames}
            and stream["frames_with_active_voice_rows"] == frames
            and stream["minimum_voices_per_active_frame"] == 2
            and stream["maximum_voices_per_active_frame"] == 2
            and stream["voice0_right_only"] and stream["voice1_left_only"])
        total, voice0, voice1, wrong_pan = activity[1]
        clear = ((activity[0][0] == 0) & (total == 2) & (voice0 == 1) & (voice1 == 1) & ~wrong_pan)
        changes = np.diff(np.concatenate(([False], clear, [False])).astype(np.int8))
        starts, ends = np.flatnonzero(changes == 1), np.flatnonzero(changes == -1)
        result["stereo_pair_only_subwindows"] = [
            {"native_start": start + int(a), "offset": int(a), "frames": int(b - a)}
            for a, b in zip(starts, ends)]
        return result


def check_path(path, fresh=False):
    path = path.absolute()
    if any(x.is_symlink() for x in (path, *path.parents)):
        raise ValueError("input/output paths must not contain symlinks")
    path = path.resolve()
    if ROOT / "build" not in path.parents:
        raise ValueError("inputs and outputs must be under this worktree build/")
    if fresh and path.exists():
        raise ValueError("output exists; choose a fresh directory")
    return path


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--reference", type=Path, required=True)
    ap.add_argument("--native", type=Path, required=True)
    ap.add_argument("--native-sound", type=Path,
                    help="counter/snd JSONL from a private EE observer; default sound.jsonl, else ticks.jsonl")
    ap.add_argument("--observer", type=Path,
                    help="completed observer/ with active rows and producer-pull clock events")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args(argv)
    try:
        ref, nat, out = check_path(args.reference), check_path(args.native), check_path(args.out, True)
        sys.path.insert(0, str(ROOT.parent / "Extermination/tools/video_compare"))
        import emrec

        rp, npth = ref / "audio.wav", nat / "audio.wav"
        ri, reference = load_pcm(rp)
        ni, native = load_pcm(npth)
        original = [x for x in load_jsonl(ref / "observations.jsonl") if x["stage"] == "play"]
        rr = with_ends(original, ri["frames"])
        native_recording = emrec.read(nat / "native.rec")
        nr = with_ends(native_recording.ticks(), ni["frames"])
        observer = (ObserverOccupancy(check_path(args.observer), nat, native_recording, ni)
                    if args.observer else None)
        sound_path = (check_path(args.native_sound) if args.native_sound else
                      nat / ("sound.jsonl" if (nat / "sound.jsonl").exists() else "ticks.jsonl"))
        snd = native_sound_rows(sound_path)
        missing_sound_rows = 0
        for row in nr:
            sound = snd.get(row["counter"])
            if sound is None:
                missing_sound_rows += 1
            else:
                row.update(sound)
        spans, rm, nm = paired_spans(rr, nr)
        mix = metrics(rp, npth, spans, ri, ni)
        mix.update(reference_coverage=coverage(spans, "reference_start", ri["frames"]),
                   native_coverage=coverage(spans, "native_start", ni["frames"]),
                   alignment="same recording segment/offset; contiguous segment spans, no resampling",
                   unmatched_reference_rows=len(rm.keys() - nm.keys()),
                   unmatched_native_rows=len(nm.keys() - rm.keys()))
        segments = [describe_window(x, rp, npth, reference, native, ri, ni) for x in spans]
        re, ne = event_groups(rr), event_groups(nr)
        matched, missing_ref, missing_nat = match_events(re, ne)
        windows, short = [], []
        for i, (r, n, delta, equally_near) in enumerate(matched):
            span = {"name": f"ee-sound-{i:03d}", "kind": "mixed_EE_age_zero_sound_window",
                    "reference_start": r["af"], "native_start": n["af"], "frames": 4800,
                    "reference_event": r, "native_event": n, "matched_tick_delta": n["off"] - r["off"],
                    "equally_near_native_groups": equally_near,
                    "unique_signature_association": equally_near == 1,
                    "alignment_evidence": {
                        "source": "same-segment EE age-zero group, matching sorted note/program/bank signatures",
                        "uncertainty_frames": LAG,
                        "additional_EE_to_SPU2_latency_bound_known": False}}
            if r["af"] + 4800 > ri["frames"] or n["af"] + 4800 > ni["frames"]:
                short.append(span)
            else:
                windows.append(describe_window(span, rp, npth, reference, native, ri, ni))
        candidate_path = ref / "voice_window_candidates.json"
        candidates = json.loads(candidate_path.read_text()).get("stereo_music_candidates", [])
        stereo, stereo_windows = [], []
        for i, candidate in enumerate(candidates):
            item = {"name": f"stereo-music-{i:02d}", "reference_candidate": candidate,
                    "kind": "stereo_sound_candidate_two_hardware_voices",
                    "isolated_voice_proven": False, "wet_history_excluded": False}
            first = rr[candidate["first_row"]]
            last = rr[candidate["last_row"]]
            first_key, last_key = (first["seg"], first["off"]), (last["seg"], last["off"])
            keys = [(r["seg"], r["off"])
                    for r in rr[candidate["first_row"]:candidate["last_row"] + 1]]
            if any(k not in nm for k in keys):
                item["eligible"] = False
                item["reason"] = "reference candidate lacks corresponding native rows"
            else:
                paired = [nm[k] for k in keys]
                item["native_pair_only_all_rows"] = all(x.get("positive_envx") == [0, 1] for x in paired)
                item["native_source_matches_all_rows"] = all(
                    x.get("stream", {}).get("active") == [2, 0, 0]
                    and x.get("stream", {}).get("mono") == 0
                    and x.get("stream", {}).get("cue") == rm[k]["stream"]["cue"]
                    for k, x in zip(keys, paired))
                item["eligible"] = bool(candidate["same_stereo_source_proven"]
                                        and item["native_pair_only_all_rows"]
                                        and item["native_source_matches_all_rows"])
                item.update(reference_start=first["af"], native_start=nm[first_key]["af"],
                            frames=min(candidate["duration_frames"],
                                       nm[last_key]["end_af"] - nm[first_key]["af"]),
                            alignment_evidence={"source": "same segment/offset; EE stereo-pair occupancy and cue",
                                                "uncertainty_frames": LAG,
                                                "additional_EE_to_SPU2_latency_bound_known": False})
                if observer:
                    item["native_observer_activity"] = observer.inspect(item["native_start"], item["frames"])
                    item["native_occupancy_source"] = "active producer rows with counter/vblank-verified sample clock"
                    item["eligible"] = bool(candidate["same_stereo_source_proven"]
                                            and item["native_source_matches_all_rows"]
                                            and item["native_observer_activity"]["stereo_pair_only"])
                    if candidate["same_stereo_source_proven"] and item["native_source_matches_all_rows"]:
                        for j, clear in enumerate(item["native_observer_activity"]["stereo_pair_only_subwindows"]):
                            if clear["frames"] < 48:
                                continue
                            window = {
                                "name": f"{item['name']}-clear-{j:02d}",
                                "parent_candidate": item["name"],
                                "kind": "stereo_sound_candidate_two_hardware_voices",
                                "reference_start": item["reference_start"] + clear["offset"],
                                "native_start": clear["native_start"], "frames": clear["frames"],
                                "eligible": True, "isolated_voice_proven": False,
                                "wet_history_excluded": False,
                                "reference_activity": "EE replies show only lane0 pair throughout parent candidate; not direct hardware activity",
                                "native_activity": "every mapped sample has exactly stream voices0/1, correct hard pan, zero active SFX producer rows",
                                "alignment_evidence": item["alignment_evidence"],
                            }
                            stereo_windows.append(describe_window(
                                window, rp, npth, reference, native, ri, ni))
                item = describe_window(item, rp, npth, reference, native, ri, ni)
            stereo.append(item)
        report = {
            "schema": "spu2-alignment-v1", "status": "diagnostic_completed",
            "reference": ri, "native": ni, "whole_mix": mix, "segments": segments,
            "sound_windows": windows, "stereo_sound_candidates": stereo,
            "stereo_sound_windows": stereo_windows,
            "native_observer_occupancy": observer.provenance if observer else None,
            "EE_sound_matching": {"reference_groups": len(re), "native_groups": len(ne),
                                  "matched_groups": len(matched), "match_within_ticks": 2,
                                  "unmatched_reference": missing_ref, "unmatched_native": missing_nat,
                                  "windows_outside_audio": short,
                                  "native_rows_missing_sound_state": missing_sound_rows,
                                  "paired_native_rows_missing_sound_state": sum(
                                      "voices" not in nm[k] for k in rm.keys() & nm.keys())},
            "limits": [
                "WAV positions are used directly: native af includes title/movie audio; reference af starts at the first post-title loop.",
                "Unmatched segment tails and noncommon title/movie spans are excluded and quantified; this is not complete whole-WAV equality.",
                "Each segment uses fixed offsets and the common available length; no time stretching, resampling, amplitude normalization or gain fitting.",
                "The 801-frame uncertainty denotes loop sampling resolution, not a proven bound on additional EE-to-SPU2 latency.",
                "Acoustic and envelope lag searches do not establish a key-on sample or correct an implementation.",
                "All sound windows use the full mix. EE stereo-pair candidates are two hardware voices; stale ENVX and wet history limit isolation.",
            ],
            "fidelity_pass": False,
        }
        out.mkdir(parents=True, exist_ok=False)
        (out / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        manifest = {"reference": str(rp), "native": str(npth),
                    "mix_spans": [dict(x, alignment_evidence={
                        "source": "paired recording segment/offset", "uncertainty_frames": LAG}) for x in spans]}
        (out / "mix_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        hashes = {}
        for path in (Path(__file__), ref / "receipt.json", ref / "observations.jsonl",
                     candidate_path, nat / "receipt.json", nat / "native.rec", sound_path):
            hashes[str(path.resolve())] = hashlib.sha256(path.read_bytes()).hexdigest()
        if observer:
            path = observer.path / "capture.json"
            hashes[str(path.resolve())] = hashlib.sha256(path.read_bytes()).hexdigest()
        (out / "inputs.json").write_text(json.dumps(hashes, indent=2) + "\n")
        print(json.dumps({"status": report["status"], "mix_max_abs": mix["max_abs"],
                          "mix_rms": mix["rms"], "paired_frames": mix["frames"],
                          "sound_windows": len(windows), "stereo_candidates_eligible": sum(
                              x["eligible"] for x in stereo),
                          "stereo_clear_subwindows": len(stereo_windows), "fidelity_pass": False}))
        return 0
    except (OSError, ValueError, KeyError, TypeError, IndexError, EOFError, wave.Error) as error:
        print(f"SPU2 alignment invalid input: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
