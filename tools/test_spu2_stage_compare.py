#!/usr/bin/env python3
"""Small synthetic stage-wiring checks; no game data or emulator needed."""
from __future__ import annotations

import ctypes as C
import json
from pathlib import Path
import struct
import time
import unittest
import wave

from spu2_stage_compare import (ROOT, ROW, Event, applied_events, compile_bridge,
                                mix_domains, observer_manifest, private_destination, run_manifest)


class StageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.out = ROOT / "build/spu2-a2/offline" / f"synthetic-{time.time_ns()}"
        cls.out.mkdir(parents=True)
        cls.lib, _ = compile_bridge(cls.out)

    def fixture(self, name):
        base = self.out / name
        base.mkdir()
        events = [
            {"type": "sfx_command", "domain": 0, "frame": 0, "seq": 0,
             "words": [1, 0, 0x3fff, 0x3fff]},
            {"type": "sfx_command", "domain": 0, "frame": 0, "seq": 1,
             "words": [3, 0, 0x000f, 0]},
            {"type": "sfx_command", "domain": 0, "frame": 0, "seq": 2,
             "words": [10, 0, 1, 0], "effective_mask": 1},
        ]
        (base / "events.jsonl").write_text("".join(json.dumps(e) + "\n" for e in events))
        records = b"".join(ROW.pack(frame, 0, 0, 0, 0, 0, 4096,
                                    0, 32767, 0x3fff, 0x3fff, 0x000f, 0,
                                    1.0, 1.0, 4096.0, frame, 0, 0)
                           for frame in range(8))
        (base / "rows.bin").write_bytes(records)
        manifest = {"schema": "spu2-stage-input-v1", "rows": "rows.bin",
                    "events": "events.jsonl",
                    "domains": [{"id": 0, "frames": 8, "initial_state": "native-model-reset",
                                 "origin_evidence": "synthetic reset"}],
                    "gaps": ["synthetic fixture has no core settings"]}
        return base, manifest, events, records

    def test_stage_values_and_unknown_core_gate(self):
        base, manifest, _, _ = self.fixture("stage-values")
        output = base / "output"
        output.mkdir()
        report = run_manifest(manifest, base, output)
        values = {}
        for stage, meta in report["domains"]["0"]["outputs"].items():
            with wave.open(meta["path"], "rb") as wav:
                values[stage] = struct.unpack("<16h", wav.readframes(8))
        # Public coefficient 0 is -1: floor(-4096/32768) = -1.
        self.assertEqual(values["gaussian"][0], -1)
        self.assertEqual(values["native_voice_sum"][0], 4096)
        # Read-before-step: the key-on frame has envelope zero.
        self.assertEqual(values["adsr"][:2], (0, 0))
        self.assertEqual(values["voice_volume"][:2], (0, 0))
        self.assertEqual(report["unavailable_stages"], ["core master/MMIX", "game reverb"])
        # No core stage exists: a manifest asking for one is rejected.
        with self.assertRaisesRegex(ValueError, "not modelled"):
            run_manifest({**manifest, "cores": [{"core": 0}]}, base, output)

    def test_applied_events_preserve_order_and_effective_key_mask(self):
        base, _, events, _ = self.fixture("events")
        events.append({"type": "sfx_command", "domain": 0, "frame": 0, "seq": 3,
                       "words": [11, 0, 1, 0], "effective_mask": 0})
        path = base / "events.jsonl"
        path.write_text("".join(json.dumps(e) + "\n" for e in events))
        result = applied_events(path)
        self.assertEqual([e.keys for e in result[0][0]], [0, 0, 1])
        events[-1]["seq"] = 1
        path.write_text("".join(json.dumps(e) + "\n" for e in events))
        with self.assertRaisesRegex(ValueError, "sequence"):
            applied_events(path)

    def test_missing_keyon_and_register_mismatch_rejected(self):
        base, _, _, raw = self.fixture("negative")
        rows = C.create_string_buffer(raw)
        output = (C.c_float * 64)()
        changes = applied_events(base / "events.jsonl")[0][0]
        no_key = (Event * 2)(*changes[:2])
        self.assertNotEqual(self.lib.spu2_stage_capture(rows, 8, no_key, 2, 8, output), 0)
        changes[0].volume[0] = 123
        wrong = (Event * len(changes))(*changes)
        self.assertNotEqual(self.lib.spu2_stage_capture(rows, 8, wrong, len(changes), 8, output), 0)

    def test_same_frame_key_order_is_not_commuted(self):
        base, _, _, raw = self.fixture("key-order")
        rows = C.create_string_buffer(raw)
        changes = applied_events(base / "events.jsonl")[0][0]
        # KON then KOFF reaches release at zero; KOFF then KON attacks.
        outputs = []
        for keys in (0x21, 0x12):
            changes[-1].keys = keys
            events = (Event * len(changes))(*changes)
            output = (C.c_float * 64)()
            self.assertEqual(self.lib.spu2_stage_capture(rows, 8, events, len(changes), 8, output), 0)
            outputs.append(tuple(output))
        self.assertNotEqual(outputs[0], outputs[1])

    def test_one_lsb_error_control_and_no_mixed_voice_claim(self):
        base, manifest, _, _ = self.fixture("error-control")
        reference = base / "reference.wav"
        with wave.open(str(reference), "wb") as wav:
            wav.setnchannels(2)
            wav.setsampwidth(2)
            wav.setframerate(48000)
            wav.writeframes(struct.pack("<16h", *([-1] * 16)))
        manifest["comparisons"] = [{"name": "synthetic", "domain": 0,
                                   "reference": str(reference), "reference_start": 0,
                                   "native_start": 0, "frames": 8,
                                   "alignment_evidence": {"source": "synthetic", "uncertainty_frames": 0}}]
        out = base / "output"
        out.mkdir()
        report = run_manifest(manifest, base, out)
        measured = report["comparisons"][0]
        self.assertTrue(measured["stages"]["gaussian"]["bit_exact"])
        self.assertIn("no per-voice", measured["scope"])
        # Preserve frame count; change exactly one reference sample by one LSB.
        with reference.open("r+b") as stream:
            stream.seek(44)
            stream.write(struct.pack("<h", 0))
        from test_spu2_output_compare import compare_spans
        native = Path(report["domains"]["0"]["outputs"]["gaussian"]["path"])
        stats = compare_spans(reference, native, [{"reference_start": 0, "native_start": 0,
                                                  "frames": 8}], 8, 8)
        self.assertEqual(stats["max_abs"], 1)
        self.assertEqual(stats["different_samples"], 1)
        self.assertAlmostEqual(stats["rms"], .25)
        self.assertFalse(stats["bit_exact"])

    def test_output_symlink_rejected(self):
        link = self.out / "escape"
        link.symlink_to("/private/tmp")
        with self.assertRaisesRegex(ValueError, "symlinks"):
            private_destination(link / "output")
        link.unlink()

    def test_initial_snapshot_and_observer_adapter(self):
        base, _, events, _ = self.fixture("initial")
        snapshots = [{"voice": v, "on": False, "pitch": 0x1000,
                      "adsr1": 15, "adsr2": 0, "volume": [0x3fff, 0x3fff],
                      "envx": 0} for v in range(48)]
        initial = {"type": "sfx_initial", "domain": 0, "frame": 0,
                   "seq": 0, "voices": snapshots}
        events = [initial, dict(events[-1], seq=1)]
        path = base / "events.jsonl"
        path.write_text("".join(json.dumps(e) + "\n" for e in events))
        normalized = applied_events(path)
        self.assertEqual(normalized[0][0][0].mask, 15)
        self.assertEqual(normalized[0][47][0].volume[0], 0x3fff)
        meta = {"schema": "spu2-native-observer-v1", "complete": True,
                "rate": 48000, "generations": [1, 1], "frames": [8, 8],
                "output_frames": [8, 8],
                "files": {"events": "events.jsonl", "voices": "rows.bin"}}
        (base / "capture.json").write_text(json.dumps(meta))
        manifest = observer_manifest(base)
        self.assertEqual(manifest["mix"]["frames"], 8)
        snapshots[0]["on"] = True
        path.write_text(json.dumps(initial) + "\n")
        with self.assertRaisesRegex(ValueError, "already playing"):
            applied_events(path)

    def test_drain_mapping_sums_domains_once_and_keeps_underrun_zero(self):
        base = self.out / "mapping"
        base.mkdir()
        for domain, value in ((0, .125), (1, .25)):
            (base / f"domain-{domain}-stages.f32").write_bytes(
                struct.pack("<32f", *([value] * 32)))
        spec = {"frames": 6, "mapping_evidence": "synthetic observed drains",
                "ranges": [{"domain": 0, "source_frame": 0, "count": 4, "output_frame": 1},
                           {"domain": 1, "source_frame": 1, "count": 2, "output_frame": 2}]}
        paths, meta = mix_domains(self.lib, base, {0: 4, 1: 4}, spec)
        with wave.open(str(paths["native_voice_sum"]), "rb") as wav:
            values = struct.unpack("<12h", wav.readframes(6))
        self.assertEqual(values[::2], (0, 4096, 12288, 12288, 4096, 0))
        self.assertEqual(meta["domain_source_coverage"]["1"]["undrained_frames"], 2)
        spec["ranges"].append(dict(spec["ranges"][0]))
        with self.assertRaisesRegex(ValueError, "overlapping"):
            mix_domains(self.lib, base, {0: 4, 1: 4}, spec)

    def test_clock_origin_and_fixed_baseline_lag_are_reused(self):
        from spu2_stage_measure import measure
        from spu2_stage_compare import digest
        base = self.out / "measure-origin"
        base.mkdir()
        def wav(name, values):
            path = base / name
            with wave.open(str(path), "wb") as out:
                out.setnchannels(2)
                out.setsampwidth(2)
                out.setframerate(48000)
                out.writeframes(struct.pack("<" + "h" * len(values), *values))
            return path
        values = tuple(range(16))
        reference = wav("reference.wav", values)
        baseline = wav("baseline.wav", (0, 0) + values)
        stage = wav("stage.wav", values)
        stage_report = base / "stages.json"
        stage_report.write_text(json.dumps({
            "mixed_outputs": {"native_voice_sum": {"path": str(stage), "sha256": digest(stage)}},
            "input_sha256": {"events": "synthetic-events"},
            "mix_mapping": {"frames": 8}, "limitations": []}))
        clock_path = base / "clock.json"
        clock = {"schema": "spu2-observer-wav-origin-v1",
                 "drain_to_native_wav_offset": 1, "native_wav_frames": 9,
                 "producer_drain_frames": [8, 8], "mix_ranges_checked": 1,
                 "waveform_validation": {"used_to_estimate_offset": False},
                 "sha256": {"wav": digest(baseline), "events": "synthetic-events"}}
        clock_path.write_text(json.dumps(clock))
        spans = [{"reference_start": 0, "native_start": 1, "frames": 8}]
        window = dict(spans[0], name="synthetic-clock",
                      alignment_evidence={"source": "synthetic counter", "uncertainty_frames": 1},
                      bounded_waveform_lag={"lag_frames": 0,
                        "comparison_span_after": {"reference_start": 1, "native_start": 2, "frames": 6}})
        alignment_path = base / "alignment.json"
        alignment_path.write_text(json.dumps({
            "reference": {"path": str(reference), "sha256": digest(reference)},
            "native": {"path": str(baseline), "sha256": digest(baseline)},
            "segments": spans, "sound_windows": [window], "limits": []}))
        result = measure(stage_report, alignment_path, clock_path)
        self.assertEqual(result["baseline_reconstruction"]["max_abs"], 0)
        self.assertEqual(result["whole_paired_mix"]["native_voice_sum"]["max_abs"], 0)
        self.assertEqual(result["windows"][0]["baseline_fixed_lag_stages"]["native_voice_sum"]["max_abs"], 0)
        self.assertFalse(result["fidelity_pass"])
        clock["waveform_validation"]["used_to_estimate_offset"] = True
        clock_path.write_text(json.dumps(clock))
        with self.assertRaisesRegex(ValueError, "without waveform"):
            measure(stage_report, alignment_path, clock_path)
        clock["waveform_validation"]["used_to_estimate_offset"] = False
        clock_path.write_text(json.dumps(clock))
        with stage.open("r+b") as stream:
            stream.seek(44)
            stream.write(struct.pack("<h", 1))
        with self.assertRaisesRegex(ValueError, "generating report"):
            measure(stage_report, alignment_path, clock_path)


if __name__ == "__main__":
    unittest.main()
