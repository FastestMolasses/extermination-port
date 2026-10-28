#!/usr/bin/env python3
"""Offline, cumulative SPU2 stage experiments over private observer captures.

Runs natively on macOS arm64. No emulator source, fitting, time stretching,
gain fitting, device output or command mutation. All WAVs remain in ignored
build/spu2-a2. This is a diagnostic, not an Original-profile replacement.

Input JSON schema "spu2-stage-input-v1":
  rows: 64-byte observer records; events: ordered JSONL applied events.
  domains: [{id: 0|1, frames: N, initial_state: "native-model-reset",
             origin_evidence: "..."}].
  comparisons: [{name, domain, reference, reference_start, native_start,
                 frames, alignment_evidence:{source,uncertainty_frames}}].
  gaps: list of explicit missing-state descriptions.
Paths are relative to the input JSON. Rows are LE:
  QII4h6H3fQII = frame,domain,voice,history[4],fraction,ENVX,VOLL,VOLR,
  ADSR1,ADSR2,legacy_gain[2],legacy_source,source_index,source_id,flags.
The rows must include every active native voice frame and lossless four-sample
source history; a nearest-sample tap alone is insufficient. Events timestamp
the frame BEFORE which they apply. Prefix data starts at model reset.

Stages retain native phase/history and lifetime: reconstructed native voice
sum; Gaussian with legacy envelope/gain; Gaussian+ADSR with legacy gain; then
integer voice volumes. Native float conversion and per-producer accumulation
can differ from the final recorded baseline: supply baseline comparisons too.
ADSR read-before-step, startup delay, interpolation-history initialization,
and the inherited PS1 reverb FIR/phase are model choices, not SPU2 evidence.

There is no core stage: core routing, master volume and the core effect
stage are not modelled, because no permitted public source documents the SPU2
mixer order or MMIX (docs/SPU2_OUTPUT.md). A manifest "cores" entry is
rejected. Unknown MMIX, master, routing or reverb state never becomes a
default setting.
Exit 0: valid experiment completed, not a fidelity pass. Exit 2: invalid input.
"""
from __future__ import annotations

import argparse
import ctypes as C
import hashlib
import json
import mmap
from pathlib import Path
import struct
import subprocess
import sys
import wave

from test_spu2_output_compare import compare_spans, evidence, integer, wav_info

ROOT = Path(__file__).resolve().parents[1]
PRIVATE = ROOT / "build/spu2-a2"
ROW = struct.Struct("<QII4h6H3fQII")
STAGES = ("native_voice_sum", "gaussian", "adsr", "voice_volume")


class Event(C.Structure):
    _fields_ = [("frame", C.c_uint64), ("mask", C.c_uint32), ("keys", C.c_uint32),
                ("adsr1", C.c_uint16), ("adsr2", C.c_uint16), ("volume", C.c_uint16 * 2)]


def digest(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        while block := stream.read(1 << 20):
            result.update(block)
    return result.hexdigest()


def private_destination(path):
    path = Path(path).absolute()
    if not path.is_relative_to(PRIVATE) or ".." in path.parts:
        raise ValueError("output must be inside this worktree's build/spu2-a2")
    if any(p.is_symlink() for p in (path, *path.parents)):
        raise ValueError("output cannot contain symlinks")
    return path


def compile_bridge(out):
    target = out / "stage-bridge.dylib"
    sources = ["tests/spu2_stage_bridge.c", "src/game/em_spu2_dsp.c"]
    command = ["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-ffp-contract=off", "-shared", "-fPIC", "-Isrc", *sources,
               "-o", str(target)]
    run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    (out / "compile.log").write_text(run.stdout + run.stderr)
    if run.returncode:
        raise ValueError("stage bridge compilation failed; see compile.log")
    lib = C.CDLL(str(target))
    ptr = C.c_void_p
    lib.spu2_stage_capture.argtypes = [ptr, C.c_size_t, ptr, C.c_size_t,
                                      C.c_size_t, ptr]
    lib.spu2_stage_capture.restype = C.c_int
    lib.spu2_stage_voice.argtypes = [ptr, C.c_size_t, ptr]
    lib.spu2_stage_voice.restype = C.c_int
    lib.spu2_stage_add.argtypes = [ptr, ptr, C.c_size_t]
    lib.spu2_stage_pcm.argtypes = [ptr, C.c_size_t, C.c_uint, ptr]
    return lib, {name: digest(ROOT / name) for name in sources}


def applied_events(path):
    result = {0: [[] for _ in range(48)], 1: [[] for _ in range(48)]}
    last_sequence = -1
    last_frame = {0: -1, 1: -1}
    with path.open() as stream:
        for line in stream:
            item = json.loads(line)
            kind = item.get("type")
            if kind not in ("sfx_command", "stream_register", "sfx_initial", "stream_initial"):
                continue
            domain = 0 if kind.startswith("sfx_") else 1
            if item.get("domain") != domain:
                raise ValueError("event domain disagrees with its type")
            frame = integer(item["frame"], "event frame")
            sequence = integer(item["seq"], "event seq")
            if sequence <= last_sequence or frame < last_frame[domain]:
                raise ValueError("applied events are not in chronological sequence")
            last_sequence, last_frame[domain] = sequence, frame

            def add(voice, mask=0, keys=0, adsr1=0, adsr2=0, left=0, right=0):
                if not 0 <= voice < 48:
                    raise ValueError("voice index outside 0..47")
                result[domain][voice].append(Event(frame, mask, keys, adsr1, adsr2,
                                                  (C.c_uint16 * 2)(left, right)))

            if kind.endswith("_initial"):
                if frame != 0:
                    raise ValueError("multiple producer generations need separate captures")
                seen = set()
                for snapshot in item["voices"]:
                    voice = snapshot["voice"]
                    if voice in seen or snapshot["on"] or snapshot["envx"]:
                        raise ValueError("initial voice snapshot is duplicated or already playing")
                    seen.add(voice)
                    add(voice, 15, adsr1=snapshot["adsr1"], adsr2=snapshot["adsr2"],
                        left=snapshot["volume"][0], right=snapshot["volume"][1])
                if seen != set(range(48)):
                    raise ValueError("initial snapshot must contain all 48 native voices")
            elif domain == 0:
                command, voice, a, b = item["words"]
                if command == 1:
                    add(voice, 12, left=a, right=b)
                elif command == 3:
                    add(voice, 3, adsr1=a, adsr2=b)
                elif command in (10, 11):
                    # Application, including ignored same-exchange KOFF, is
                    # captured by the driver observer rather than guessed here.
                    mask = integer(item["effective_mask"], "effective key mask")
                    if mask >> 48:
                        raise ValueError("key mask exceeds 48 voices")
                    for v in range(48):
                        if mask >> v & 1:
                            add(v, keys=1 if command == 10 else 2)
            else:
                reg, value = item["reg"], item["value"]
                if item["kind"] == "param" and not reg & 0x80:
                    voice = (reg & 1) * 24 + ((reg >> 1) & 31)
                    selector = reg & 0xff00
                    if selector == 0:
                        add(voice, 4, left=value)
                    elif selector == 0x100:
                        add(voice, 8, right=value)
                    elif selector == 0x300:
                        add(voice, 1, adsr1=value)
                    elif selector == 0x400:
                        add(voice, 2, adsr2=value)
                elif item["kind"] == "switch":
                    if reg & 0xfffe not in (0x1500, 0x1600):
                        continue
                    for v in range(24):
                        if value >> v & 1:
                            add((reg & 1) * 24 + v,
                                keys=1 if reg & 0xfffe == 0x1500 else 2)
    return result


def observer_manifest(capture):
    metadata_path = capture / "capture.json"
    metadata = json.loads(metadata_path.read_text())
    if metadata.get("schema") != "spu2-native-observer-v1":
        raise ValueError("unsupported observer capture schema")
    if not metadata.get("complete") or metadata.get("rate") != 48000:
        raise ValueError("observer capture is incomplete or has the wrong rate")
    if metadata["generations"] != [1, 1]:
        raise ValueError("multiple producer generations need explicit epoch mapping")
    event_path = capture / metadata["files"]["events"]
    ranges = []
    with event_path.open() as stream:
        for line in stream:
            item = json.loads(line)
            if item.get("type") == "mix_range" and item["count"]:
                ranges.append({k: item[k] for k in ("domain", "source_frame", "count", "output_frame")})
    return {
        "schema": "spu2-stage-input-v1",
        "rows": str(capture / metadata["files"]["voices"]), "events": str(event_path),
        "domains": [{"id": d, "frames": metadata["frames"][d],
                     "initial_state": "native-model-reset",
                     "origin_evidence": "observer initial snapshots; native reset, not hardware reset"}
                    for d in (0, 1)],
        "mix": {"frames": max(metadata["output_frames"]), "ranges": ranges,
                "mapping_evidence": "observed source ring drain ranges, before downstream float sum"},
        "observer_metadata_sha256": digest(metadata_path),
        "gaps": [
            "Original SPU2 key-on sample timestamps not captured.",
            "Legacy active-row lifetime bounds available source history.",
            "Startup/movie producers are not captured; compare verified gameplay spans only.",
            metadata.get("unknown_reason", "Core state requires independent original initialization evidence."),
        ],
    }


def split_rows(path, out, domains):
    """Keep original rows private; only counts and hashes enter receipts."""
    if path.stat().st_size % ROW.size:
        raise ValueError("truncated 64-byte voice record")
    streams, counts, previous = {}, {}, {}
    try:
        with path.open("rb") as source:
            while block := source.read(64 * 16384):
                for offset in range(0, len(block), 64):
                    frame, domain, voice = struct.unpack_from("<QII", block, offset)
                    if domain not in domains or voice >= 48 or frame >= domains[domain]:
                        raise ValueError("voice row outside its declared domain/frame/voice")
                    key = domain, voice
                    if frame <= previous.get(key, -1):
                        raise ValueError("duplicate or unordered active voice frame")
                    previous[key] = frame
                    if key not in streams:
                        streams[key] = (out / f"domain-{domain}-voice-{voice}.rows").open("xb")
                    streams[key].write(block[offset:offset + 64])
                    counts[key] = counts.get(key, 0) + 1
    finally:
        for stream in streams.values():
            stream.close()
    return counts


def write_pcm(path, pcm, frames):
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(48000)
        wav.writeframes(C.string_at(pcm, frames * 4))


def stage_wavs(lib, out, prefix, samples, frames):
    pcm = (C.c_int16 * (frames * 2))()
    result = {}
    for stage, name in enumerate(STAGES):
        lib.spu2_stage_pcm(samples, frames, stage, pcm)
        path = out / f"{prefix}-{name}.wav"
        write_pcm(path, pcm, frames)
        result[name] = path
    return result


def render_domain(lib, out, domain, frames, counts, events):
    total = (C.c_float * (frames * 8))()
    voices = []
    for (d, voice), count in sorted(counts.items()):
        if d != domain:
            continue
        changes = [e for e in events[voice] if e.frame < frames]
        if not changes:
            raise ValueError("active voice has no applied register/key-on prefix")
        packed = (Event * len(changes))(*changes)
        values = (C.c_float * (frames * 8))()
        path = out / f"domain-{domain}-voice-{voice}.rows"
        with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_COPY) as rows:
            pointer = (C.c_char * len(rows)).from_buffer(rows)
            status = lib.spu2_stage_capture(pointer, count, packed, len(changes), frames, values)
            del pointer
        if status:
            raise ValueError(f"domain {domain} voice {voice}: missing prefix, invalid history "
                             "or captured registers disagree with applied event timeline")
        lib.spu2_stage_add(total, values, frames)
        voice_pcm = (C.c_int16 * (frames * 2))()
        lib.spu2_stage_pcm(values, frames, 3, voice_pcm)
        voice_path = out / f"domain-{domain}-voice-{voice}-voice_volume.wav"
        write_pcm(voice_path, voice_pcm, frames)
        voices.append({"voice": voice, "source_rows": count, "output": str(voice_path),
                       "sha256": digest(voice_path),
                       "scope": "native diagnostic voice; not an isolated original comparison"})
    float_path = out / f"domain-{domain}-stages.f32"
    with float_path.open("xb") as stream:
        stream.write(C.string_at(total, frames * 8 * 4))
    return stage_wavs(lib, out, f"domain-{domain}", total, frames), voices


def mix_domains(lib, out, domains, specification):
    """Use recorded drain positions, never a waveform-derived time shift."""
    frames = integer(specification["frames"], "mix frames", 1)
    if not specification.get("mapping_evidence"):
        raise ValueError("output mapping evidence is required")
    total = (C.c_float * (frames * 8))()
    source_intervals, output_intervals = {}, {}
    coverage = {d: 0 for d in domains}
    for number, item in enumerate(specification["ranges"]):
        domain = item["domain"]
        source = integer(item["source_frame"], "source_frame")
        target = integer(item["output_frame"], "output_frame")
        count = integer(item["count"], "count", 1)
        if domain not in domains or source + count > domains[domain] or target + count > frames:
            raise ValueError("mix range exceeds a source/output timeline")
        for table, start in ((source_intervals, source), (output_intervals, target)):
            intervals = table.setdefault(domain, [])
            if any(start < end and old < start + count for old, end in intervals):
                raise ValueError("overlapping mix ranges for one producer domain")
            intervals.append((start, start + count))
        with (out / f"domain-{domain}-stages.f32").open("rb") as stream:
            stream.seek(source * 32)
            data = stream.read(count * 32)
        if len(data) != count * 32:
            raise ValueError("truncated source stage floats")
        source_values = (C.c_float * (count * 8)).from_buffer_copy(data)
        lib.spu2_stage_add(C.byref(total, target * 32), source_values, count)
        coverage[domain] += count
    paths = stage_wavs(lib, out, "output-mix", total, frames)
    return paths, {
        "frames": frames, "mapping_evidence": specification["mapping_evidence"],
        "domain_source_coverage": {str(d): {"rendered_frames": domains[d],
                                           "drained_frames": coverage[d],
                                           "undrained_frames": domains[d] - coverage[d]}
                                   for d in domains},
        "ranges": len(specification["ranges"]),
        "scope": "observed producer drain map; omitted producers remain unmodelled",
    }


def run_manifest(manifest, base, out):
    if manifest.get("schema") != "spu2-stage-input-v1":
        raise ValueError("unsupported stage input schema")
    if manifest.get("cores"):
        raise ValueError("core stages are not modelled (no permitted public mixer source)")
    domains = {}
    for entry in manifest["domains"]:
        domain = integer(entry["id"], "domain")
        if domain not in (0, 1) or domain in domains:
            raise ValueError("invalid/duplicate domain")
        if entry.get("initial_state") != "native-model-reset" or not entry.get("origin_evidence"):
            raise ValueError("complete native reset prefix and origin evidence are required")
        domains[domain] = integer(entry["frames"], "domain frames", 1)
    if not domains:
        raise ValueError("at least one domain is required")
    rows, events_path = (base / manifest[k] for k in ("rows", "events"))
    lib, source_hashes = compile_bridge(out)
    events = applied_events(events_path)
    counts = split_rows(rows, out, domains)
    outputs, voices = {}, {}
    for domain, frames in domains.items():
        outputs[domain], voices[domain] = render_domain(lib, out, domain, frames,
                                                       counts, events[domain])
    mixed_paths, mixed_metadata = {}, None
    if manifest.get("mix"):
        mixed_paths, mixed_metadata = mix_domains(lib, out, domains, manifest["mix"])
    comparisons = []
    for window in manifest.get("comparisons", []):
        domain = window.get("domain")
        proof = evidence(window["alignment_evidence"], window["name"])
        reference = (base / window["reference"]).resolve()
        ri = wav_info(reference)
        span = {key: integer(window[key], key, 1 if key == "frames" else 0)
                for key in ("reference_start", "native_start", "frames")}
        measured = {}
        if domain is None:
            if not mixed_paths:
                raise ValueError("whole-mix comparison requires recorded drain ranges")
            paths = dict(mixed_paths)
        else:
            paths = dict(outputs[domain])
        if window.get("baseline"):
            paths["recorded_baseline"] = (base / window["baseline"]).resolve()
        for name, path in paths.items():
            ni = wav_info(path)
            stats = compare_spans(reference, path, [span], ri["frames"], ni["frames"])
            stats["zero_uncertainty"] = proof["uncertainty_frames"] == 0
            measured[name] = stats
        comparisons.append({"name": window["name"], "domain": domain, "reference": ri,
                            "scope": ("mixed reference versus mapped native producers; "
                                      if domain is None else
                                      "mixed reference versus one native producer domain; ") +
                                     "no per-voice original exactness claim",
                            "alignment_evidence": proof, "stages": measured})
    return {
        "schema": "spu2-stage-report-v1", "rate": 48000,
        "input_sha256": {"rows": digest(rows), "events": digest(events_path)},
        "source_sha256": source_hashes,
        "tool_sha256": digest(Path(__file__)),
        "domains": {str(d): {"frames": domains[d],
                              "outputs": {k: wav_info(v) for k, v in paths.items()},
                              "voices": voices[d]} for d, paths in outputs.items()},
        "comparisons": comparisons,
        "mixed_outputs": {k: wav_info(v) for k, v in mixed_paths.items()},
        "mix_mapping": mixed_metadata,
        "unavailable_stages": ["core master/MMIX", "game reverb"],
        "gaps": manifest.get("gaps", []),
        "limitations": [
            "Native producer phase/history retained; active rows follow legacy envelope lifetime.",
            "Applied native key-on timestamps are not measured original SPU2 sample boundaries.",
            "Gaussian/ADSR initialization and read-before-step phase remain model choices.",
            "Domain sums exclude the other producer, external inputs and reverb unless separately supplied.",
            "No waveform alignment or gain fit; numerical equality is scoped to supplied samples only.",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--manifest", type=Path)
    inputs.add_argument("--capture", type=Path, help="Finalized private observer capture directory")
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    try:
        out = private_destination(args.out)
        out.mkdir(parents=True, exist_ok=False)
        if args.capture:
            manifest = observer_manifest(args.capture.resolve())
            manifest_path = out / "input.json"
            manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
        else:
            manifest_path = args.manifest
            manifest = json.loads(manifest_path.read_text())
        report = run_manifest(manifest, manifest_path.resolve().parent, out)
        report["manifest_sha256"] = digest(manifest_path)
        (out / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        for row in report["comparisons"]:
            for stage, stats in row["stages"].items():
                print(f"{row['name']} {stage}: max={stats['max_abs']} RMS={stats['rms']:.9f}")
        print("SPU2 staged diagnostic complete; this exit status is not a fidelity certification.")
        return 0
    except (OSError, ValueError, KeyError, TypeError, OverflowError, wave.Error) as exc:
        print(f"SPU2 staged diagnostic invalid: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
