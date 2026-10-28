#!/usr/bin/env python3
"""Synthetic checks for aligned audio diagnostics; no game captures required.

Run on macOS with ../Extermination/.venv/bin/python. Temporary WAVs and observer
rows are generated under ignored build/ and removed after the checks.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
import wave

import numpy as np

import capture_spu2_aligned as capture
import compare_spu2_alignment as aligned
from test_spu2_output_compare import compare_spans, wav_info

ROOT = Path(__file__).resolve().parents[1]


def write_wav(path, samples):
    with wave.open(str(path), "wb") as stream:
        stream.setnchannels(2)
        stream.setsampwidth(2)
        stream.setframerate(48000)
        stream.writeframes(np.asarray(samples, dtype="<i2").tobytes())


class AlignmentTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="spu2-alignment-test-", dir=ROOT / "build")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def test_signed_lag_and_silent_tie(self):
        rng = np.random.default_rng(871)
        reference = rng.integers(-1000, 1000, (8000, 2), dtype=np.int16)
        later = np.zeros_like(reference)
        later[73:] = reference[:-73]
        self.assertEqual(aligned.best_wave_lag(reference, later, 801)["lag_frames"], 73)
        earlier = np.zeros_like(reference)
        earlier[:-45] = reference[45:]
        self.assertEqual(aligned.best_wave_lag(reference, earlier, 801)["lag_frames"], -45)
        silent = np.zeros_like(reference)
        self.assertEqual(aligned.best_wave_lag(silent, silent, 801)["lag_frames"], 0)
        self.assertIsNone(aligned.best_wave_lag(reference[:80], reference[:80], 801))

    def test_mutation_is_not_fitted_away(self):
        rng = np.random.default_rng(872)
        reference = rng.integers(-900, 900, (4800, 2), dtype=np.int16)
        changed = reference.copy()
        changed[2400, 1] += 1
        rp, same, mutated = (self.root / name for name in ("reference.wav", "same.wav", "mutated.wav"))
        write_wav(rp, reference)
        write_wav(same, reference)
        write_wav(mutated, changed)
        spans = [{"reference_start": 0, "native_start": 0, "frames": 4800}]
        self.assertTrue(compare_spans(rp, same, spans, 4800, 4800)["bit_exact"])
        result = compare_spans(rp, mutated, spans, 4800, 4800)
        self.assertFalse(result["bit_exact"])
        self.assertEqual(result["max_abs"], 1)
        self.assertEqual(result["different_samples"], 1)
        self.assertGreater(result["rms"], 0)

    def test_segment_tail_and_duplicate_guard(self):
        reference = [{"seg": 2, "off": i, "af": i * 800, "end_af": (i + 1) * 800}
                     for i in range(4)]
        native = [{"seg": 2, "off": i, "af": 100 + i * 800, "end_af": 100 + (i + 1) * 800}
                  for i in range(3)]
        spans, rm, nm = aligned.paired_spans(reference, native)
        self.assertEqual(len(spans), 1)
        self.assertEqual(spans[0]["frames"], 2400)
        self.assertEqual(spans[0]["native_start"], 100)
        self.assertEqual(rm.keys() - nm.keys(), {(2, 3)})
        with self.assertRaises(ValueError):
            aligned.unique_map(reference + [reference[0]])

    def test_event_identity_and_tick_boundary(self):
        voice = {"voice": 3, "serial": 8, "state": 1, "note": 48, "owner": 2,
                 "kind": 2, "age": 0, "bank_handle": 4, "program": 1, "envx_reply": 0}
        rows = [{"counter": 100 + i, "seg": 4, "off": i, "af": 800 * i, "voices": [voice]}
                for i in range(3)]
        groups = aligned.event_groups(rows)
        self.assertEqual(len(groups), 1)  # repeated age zero is one serial
        self.assertEqual(len(aligned.match_events(groups, [dict(groups[0], off=2)])[0]), 1)
        self.assertEqual(len(aligned.match_events(groups, [dict(groups[0], off=3)])[0]), 0)
        ambiguous = aligned.match_events(groups, [dict(groups[0]), dict(groups[0])])[0]
        self.assertEqual(ambiguous[0][3], 2)

    def test_stream_load_is_not_lane_state(self):
        seq, lanes = bytearray(64), bytearray(288)
        seq[4:7] = bytes([2, 0, 0])
        seq[7:9] = bytes([2, 1])
        struct.pack_into("<3i", seq, 0x28, 63, 0, 0)
        lanes[0], lanes[3] = 1, 2
        struct.pack_into("<Q", lanes, 8, 3)
        result = capture.parse_stream(seq, lanes)
        self.assertEqual(result["active"], [2, 0, 0])
        self.assertEqual(result["cue"], [63, 0, 0])
        self.assertEqual(result["lanes"][0]["state"], 1)
        self.assertEqual(result["lanes"][0]["load"], 2)
        self.assertEqual(result["lanes"][0]["voice_mask"], 3)
        with self.assertRaises(ValueError):
            capture.parse_stream(seq, lanes[:-1])
        seq[8] = 3
        with self.assertRaises(ValueError):
            capture.parse_stream(seq, lanes)

    def test_fresh_output_and_json_guards(self):
        with self.assertRaises(ValueError):
            aligned.check_path(self.root, True)
        link = self.root / "alias"
        link.symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(ValueError):
            aligned.check_path(link / "new-output", True)
        with self.assertRaises(ValueError):
            capture.fresh_output(link / "new-output", ROOT.parent / "Extermination")
        bad = self.root / "malformed.jsonl"
        bad.write_text("[]\n")
        with self.assertRaises(ValueError):
            aligned.load_jsonl(bad)

    def observer_fixture(self, name, sfx=False, counter_error=False):
        native = self.root / name
        observer = native / "observer"
        observer.mkdir(parents=True)
        write_wav(native / "audio.wav", np.zeros((1000, 2), dtype=np.int16))
        (native / "native.rec").write_text("synthetic identical recorder identity\n")
        events = []
        for step in (1, 2):
            for domain in (0, 1):
                events.append({"type": "mix_range", "domain": domain,
                               "counter": 99 if counter_error else step,
                               "vblank": step + 1, "output_frame": (step - 1) * 100,
                               "source_frame": (step - 1) * 100,
                               "count": 100, "requested": 100})
        (observer / "events.jsonl").write_text("".join(json.dumps(x) + "\n" for x in events))
        dtype = np.dtype({"names": ["frame", "domain", "voice", "gain_l", "gain_r"],
                          "formats": ["<u8", "<u4", "<u4", "<f4", "<f4"],
                          "offsets": [0, 8, 12, 36, 40], "itemsize": 64})
        rows = np.zeros(400 + bool(sfx), dtype=dtype)
        rows["frame"][:400] = np.repeat(np.arange(200), 2)
        rows["domain"][:400] = 1
        rows["voice"][:400] = np.tile([0, 1], 200)
        rows["gain_r"][:400:2] = 1
        rows["gain_l"][1:400:2] = 1
        if sfx:
            rows[-1]["frame"], rows[-1]["domain"], rows[-1]["voice"] = 75, 0, 32
        raw = rows.tobytes()
        (observer / "voice-frames.bin").write_bytes(raw)
        metadata = {"complete": True, "voice_record_bytes": 64, "voice_records": len(rows),
                    "generations": [1, 1], "output_frames": [200, 200],
                    "files": {"events": "events.jsonl", "voices": "voice-frames.bin"},
                    "sha256": {"voice-frames.bin": hashlib.sha256(raw).hexdigest(),
                               "events.jsonl": aligned.digest(observer / "events.jsonl")}}
        (observer / "capture.json").write_text(json.dumps(metadata))
        recording = SimpleNamespace(rows=[{"step": i, "counter": i, "af": 800 + (i - 1) * 100}
                                          for i in (1, 2)])
        return observer, native, recording, wav_info(native / "audio.wav")

    def test_observer_clock_and_stereo_activity(self):
        obj = aligned.ObserverOccupancy(*self.observer_fixture("pair"))
        self.assertEqual(obj.offset, 800)
        result = obj.inspect(850, 100)
        self.assertTrue(result["stereo_pair_only"])
        self.assertEqual(result["domains"]["1"]["voice_row_counts"], {"0": 100, "1": 100})
        with self.assertRaises(ValueError):
            obj.inspect(999, 2)
        mixed = aligned.ObserverOccupancy(*self.observer_fixture("mixed", sfx=True))
        activity = mixed.inspect(850, 100)
        self.assertFalse(activity["stereo_pair_only"])
        self.assertEqual(activity["stereo_pair_only_subwindows"],
                         [{"native_start": 850, "offset": 0, "frames": 25},
                          {"native_start": 876, "offset": 26, "frames": 74}])
        with self.assertRaises(ValueError):
            aligned.ObserverOccupancy(*self.observer_fixture("bad-clock", counter_error=True))

    def test_observer_changed_payload_rejected(self):
        for role in ("events.jsonl", "voice-frames.bin"):
            args = self.observer_fixture("changed-" + role)
            path = args[0] / role
            data = bytearray(path.read_bytes())
            data[-1] ^= 1
            path.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "hash differs"):
                aligned.ObserverOccupancy(*args)


if __name__ == "__main__":
    unittest.main()
