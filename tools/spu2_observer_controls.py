#!/usr/bin/env python3
"""Normalize captured core-control requests for private offline experiments.

Native macOS arm64; standard library and the existing original IOP oracle.
The native dry mixer did not apply EON. The original driver executes each
distinct observed EON command to establish its LIBSD setter calls. Applied
EVOL writes come directly from the native observer. Independent producer
clocks are mapped through recorded successful ring drains, never assumed equal.
No game, emulator or device run is started. Payloads remain in ignored build/.
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import json
from pathlib import Path
import struct
import sys
import wave

from export_spu2_effects import capture_iop
from test_iop_stream_reference import DECOMP, DriverOracle, iso_file

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as source:
        while data := source.read(1 << 20):
            value.update(data)
    return value.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--observer", type=Path, required=True)
    parser.add_argument("--initialization", type=Path, required=True)
    parser.add_argument("--native-recording", type=Path, required=True)
    parser.add_argument("--native-wav", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--iso", type=Path, default=DECOMP / "Extermination-rebuilt.iso")
    parser.add_argument("--iop", type=Path, default=DECOMP /
                        "build/startup-reference/fork-states/beats/s87/route/00_panel_no_battery/state.p2s")
    args = parser.parse_args()
    out = args.out.resolve()
    if not out.is_relative_to(ROOT / "build") or out.exists():
        parser.error("choose a fresh private JSON path under this worktree's build/")
    events_path = args.observer / "events.jsonl"
    capture = json.loads((args.observer / "capture.json").read_text())
    assert capture["complete"] and capture["generations"] == [1, 1]
    assert digest(events_path) == capture["sha256"]["events.jsonl"]
    events = [json.loads(line) for line in events_path.open()]
    sys.path.insert(0, str(DECOMP / "tools/video_compare"))
    import emrec
    recorded = {r["step"]: r for r in emrec.read(args.native_recording).rows}
    with wave.open(str(args.native_wav), "rb") as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 2, 48000)
        wav_frames = wav.getnframes()
    offsets, observed_steps = set(), {0: set(), 1: set()}
    mix_rows = [e for e in events if e["type"] == "mix_range"]
    for event in mix_rows:
        step = event["vblank"] - 1
        row = recorded[step]
        assert row["counter"] == event["counter"]
        assert step not in observed_steps[event["domain"]], "multiple pulls need explicit chunk mapping"
        observed_steps[event["domain"]].add(step)
        offsets.add(row["af"] - event["output_frame"])
        end = recorded[step + 1]["af"] if step + 1 in recorded else wav_frames
        assert end - row["af"] == event["requested"]
    assert len(offsets) == 1, "producer drain has more than one WAV origin"
    origin = offsets.pop()
    assert origin >= 0
    for domain in (0, 1):
        assert origin + capture["output_frames"][domain] == wav_frames
        assert observed_steps[domain] == set(range(min(observed_steps[domain]), len(recorded)))
    with wave.open(str(args.native_wav), "rb") as wav:
        prefix_zero = not any(wav.readframes(origin))
    # The offset comes from replay counters/positions; silence is only a
    # validation of the code path, never a fitted shift.
    assert prefix_zero
    clock = {
        "schema": "spu2-observer-wav-origin-v1",
        "drain_to_native_wav_offset": origin, "native_wav_frames": wav_frames,
        "producer_drain_frames": capture["output_frames"],
        "mix_ranges_checked": len(mix_rows), "recording_rows": len(recorded),
        "method": "Every mix_range matches native.rec step=vblank-1 and counter; row.af-output_frame is constant.",
        "code_evidence": [
            "em_replay.c filter saves af before audio_step; audio_step writes every requested sample.",
            "em_bgm.c em_bgm_render_offline writes zeros before offline_running, without calling producer mixers.",
            "Producer output_frame counters begin at the first actual mixer callback, so the earlier silent pull is absent.",
        ],
        "waveform_validation": {"initial_gap_is_zero": prefix_zero,
                                "used_to_estimate_offset": False},
        "sha256": {"events": digest(events_path), "recording": digest(args.native_recording),
                   "wav": digest(args.native_wav)},
    }
    ranges = {}
    for domain in (0, 1):
        rows = [e for e in events if e["type"] == "mix_range" and
                e["domain"] == domain and e["count"]]
        for previous, current in zip(rows, rows[1:]):
            assert previous["source_frame"] + previous["count"] <= current["source_frame"]
            assert previous["output_frame"] + previous["count"] <= current["output_frame"]
        ranges[domain] = rows, [e["source_frame"] for e in rows]

    def output_frame(domain, frame):
        rows, starts = ranges[domain]
        index = bisect.bisect_right(starts, frame) - 1
        if index < 0:
            return None
        row = rows[index]
        if frame >= row["source_frame"] + row["count"]:
            return None
        return row["output_frame"] + frame - row["source_frame"]

    observed = [e for e in events if e["type"] == "sfx_command" and e["words"][0] == 12]
    _, module = iso_file(args.iso, "/IRX/SNDN2DRV.IRX")
    memory = capture_iop(args.iop)
    oracle = DriverOracle(module, memory)
    normalized, tested = {}, 0
    # Asymmetric independent control avoids a zero/symmetric capture masking
    # an incorrect core or left/right mapping.
    commands = {tuple(e["words"]) for e in observed} | {(12, 0, 0x123456, 0x654321)}
    for words in sorted(commands):
        cpu = oracle.cpu(([0] * 48, [0] * 48, 1))
        pointer = oracle.base + 0x34b0
        cpu.write(pointer, struct.pack("<4I", *words))
        assert cpu.call(oracle.base + 0x634, (pointer, 16, 0)) == 0
        expected = [(7, 0x1900, words[2], 0, 0, 0),
                    (7, 0x1b00, words[2], 0, 0, 0),
                    (7, 0x1901, words[3], 0, 0, 0),
                    (7, 0x1b01, words[3], 0, 0, 0)]
        assert cpu.log == expected, "original EON setter mapping changed"
        normalized[words] = [(0, "wet_left", words[2]), (0, "wet_right", words[2]),
                             (1, "wet_left", words[3]), (1, "wet_right", words[3])]
        tested += 1
    changes, unmapped = [], []

    def add(event, values, origin_label):
        mapped = output_frame(event["domain"], event["frame"])
        for core, field, value in values:
            row = {"output_frame": mapped, "source_domain": event["domain"],
                   "source_frame": event["frame"], "source_seq": event["seq"],
                   "native_wav_frame": None if mapped is None else mapped + origin,
                   "core": core, "field": field, "value": value, "origin": origin_label}
            (unmapped if mapped is None else changes).append(row)

    for event in events:
        if event["type"] == "sfx_command" and event["words"][0] == 12:
            add(event, normalized[tuple(event["words"])],
                "original-driver-command-semantics; native request only")
        elif event["type"] == "stream_register" and event["kind"] == "param":
            register = event["reg"]
            selector = register & 0xfffe
            if selector in (0xb80, 0xc80):
                field = "effect_return_left" if selector == 0xb80 else "effect_return_right"
                add(event, [(register & 1, field, event["value"])],
                    "native applied stream_register")
    changes.sort(key=lambda e: (e["output_frame"], e["source_seq"], e["core"], e["field"]))
    absent = {}
    for command in (13, 0x14, 0x15, 0x17, 0x18):
        absent[hex(command)] = sum(e["type"] in ("sfx_command", "stream_command") and
                                  e["words"][0] == command for e in events)
    initial = json.loads(args.initialization.read_text())
    result = {
        "schema": "spu2-observer-core-controls-v1", "rate": 48000,
        "output_frames": capture["output_frames"], "initial_state": initial,
        "output_clock": "producer-drain-relative; add clock_origin offset for native WAV",
        "clock_origin": clock,
        "changes": changes, "unmapped": unmapped,
        "oracle": {"distinct_eon_cases": tested, "asymmetric_control": True,
                   "driver_sha256": hashlib.sha256(module).hexdigest(),
                   "resident_iop_sha256": hashlib.sha256(memory).hexdigest(),
                   "setter_mapping_exact": True},
        "capture_command_counts": absent,
        "provenance": {"events_sha256": digest(events_path),
                       "initialization_sha256": digest(args.initialization),
                       "observer_capture_sha256": digest(args.observer / "capture.json"),
                       "tool_sha256": digest(Path(__file__))},
        "limits": [
            "Initial core setup is an original-instruction result, not live captured hardware state.",
            "EON requests were recorded but ignored by the native dry mixer; offline wet routing is experimental.",
            "Native does not implement all original core configuration, so absence of a captured write is not independent proof of no original runtime write.",
            "No runtime mode, area, master, MMIX or PMON write appears in this observer capture.",
            "Initial mode/area state must retain its own initialization evidence; missing controls are not invented.",
            "Output mapping uses each independent producer's successful ring drains; unmapped events are not extrapolated.",
            "A request timestamp from the native model is not an original SPU2 sample timestamp.",
        ]}
    out.parent.mkdir(parents=True, exist_ok=True)
    clock_path = out.with_name(out.stem + "-clock.json")
    if clock_path.exists():
        raise ValueError("clock receipt already exists")
    clock_path.write_text(json.dumps(clock, indent=2) + "\n")
    out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"output": str(out), "mapped_changes": len(changes),
                      "unmapped_changes": len(unmapped), "eon_oracle_cases": tested,
                      "clock_receipt": str(clock_path), "wav_origin": origin,
                      "payload_printed": False}))


if __name__ == "__main__":
    main()
