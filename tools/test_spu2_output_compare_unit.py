#!/usr/bin/env python3
"""Fast synthetic controls for the opt-in SPU2 comparison; no assets needed."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import wave

from test_spu2_output_compare import compare_manifest

PROOF = {"source": "synthetic exact key-on fixture", "uncertainty_frames": 0}


def write_wav(path, pairs, rate=48000):
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(b"".join(struct.pack("<hh", *pair) for pair in pairs))


class CompareTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="spu2-compare-")
        self.base = Path(self.temp.name)
        self.pairs = [(-32768, 32767), (-3, 7), (0, 0), (123, -456)]
        write_wav(self.base / "reference.wav", self.pairs)
        write_wav(self.base / "native.wav", self.pairs)
        self.manifest = {"reference": "reference.wav", "native": "native.wav",
                         "alignment_evidence": PROOF}

    def tearDown(self):
        self.temp.cleanup()

    def compare(self):
        return compare_manifest(self.manifest, self.base)

    def test_identical_and_one_lsb_mutation_control(self):
        self.assertTrue(self.compare()["all_bit_exact_with_complete_alignment"])
        mutated = self.pairs.copy()
        mutated[2] = (0, 1)
        write_wav(self.base / "native.wav", mutated)
        report = self.compare()
        self.assertFalse(report["all_bit_exact_with_complete_alignment"])
        self.assertEqual(report["whole_mix"]["max_abs"], 1)
        self.assertEqual(report["whole_mix"]["different_samples"], 1)
        self.assertAlmostEqual(report["whole_mix"]["rms"], (1 / 8) ** 0.5)
        self.assertEqual(report["whole_mix"]["first_difference"]["native_frame"], 2)
        manifest = self.base / "manifest.json"
        manifest.write_text(json.dumps(self.manifest))
        run = subprocess.run([sys.executable, str(Path(__file__).with_name("test_spu2_output_compare.py")),
                              "--manifest", str(manifest)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 1, run.stdout + run.stderr)

    def test_extreme_difference_does_not_overflow(self):
        write_wav(self.base / "native.wav", [(32767, -32768)] + self.pairs[1:])
        mix = self.compare()["whole_mix"]
        self.assertEqual(mix["max_abs"], 65535)
        self.assertAlmostEqual(mix["rms"], 32767.5)

    def test_first_difference_uses_frame_then_channel(self):
        write_wav(self.base / "native.wav", [(-32768, 32766), (-2, 7)] + self.pairs[2:])
        first = self.compare()["whole_mix"]["first_difference"]
        self.assertEqual((first["native_frame"], first["channel"]), (0, 1))

    def test_event_aligns_by_key_on_without_hiding_mix_difference(self):
        write_wav(self.base / "native.wav", [(0, 0)] + self.pairs)
        self.manifest["events"] = [{"name": "shifted", "reference_key_on": 0,
                                   "native_key_on": 1, "frames": 4, "alignment_evidence": PROOF}]
        report = self.compare()
        self.assertTrue(report["events"][0]["bit_exact"])
        self.assertEqual(report["events"][0]["kind"], "mixed_event_window")
        self.assertEqual(report["isolated_voice_count"], 0)
        self.assertFalse(report["all_bit_exact_with_complete_alignment"])
        self.assertEqual(report["whole_mix"]["native_coverage"]["uncompared_frames"], 1)

    def test_mixed_capture_cannot_claim_per_voice(self):
        self.manifest["events"] = [{"name": "not isolated", "reference_key_on": 0,
                                   "native_key_on": 0, "frames": 4, "alignment_evidence": PROOF,
                                   "isolated_voice_evidence": {"voice": 0, "source": "assertion",
                                                              "reference_active_voices": 1,
                                                              "native_active_voices": 1}}]
        with self.assertRaisesRegex(ValueError, "separate isolated captures"):
            self.compare()

    def test_uncertainty_is_not_exactness(self):
        self.manifest["alignment_evidence"] = {"source": "tick bracket", "uncertainty_frames": 801}
        report = self.compare()
        self.assertTrue(report["whole_mix"]["bit_exact"])
        self.assertFalse(report["all_bit_exact_with_complete_alignment"])

    def test_malformed_entries_are_invalid_input_exit_two(self):
        for field, value in (("events", {}), ("events", [None]), ("events", ["bad"]),
                             ("events", [7]), ("mix_spans", [None]), ("mix_spans", ["bad"]),
                             ("mix_spans", [7])):
            with self.subTest(field=field, value=value):
                data = {**self.manifest, field: value}
                manifest = self.base / "manifest.json"
                manifest.write_text(json.dumps(data))
                run = subprocess.run(
                    [sys.executable, str(Path(__file__).with_name("test_spu2_output_compare.py")),
                     "--manifest", str(manifest)], capture_output=True, text=True)
                self.assertEqual(run.returncode, 2, run.stdout + run.stderr)
                self.assertNotIn("Traceback", run.stderr)

    def test_report_cannot_overwrite_input_via_symlink(self):
        manifest = self.base / "manifest.json"
        manifest.write_text(json.dumps(self.manifest))
        link = self.base / "report.json"
        link.symlink_to(self.base / "reference.wav")
        before = (self.base / "reference.wav").read_bytes()
        run = subprocess.run(
            [sys.executable, str(Path(__file__).with_name("test_spu2_output_compare.py")),
             "--manifest", str(manifest), "--report", str(link)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 2, run.stdout + run.stderr)
        self.assertEqual((self.base / "reference.wav").read_bytes(), before)

    def test_refuse_out_of_bounds_truncation_overlap_and_wrong_rate(self):
        self.manifest["mix_spans"] = [{"reference_start": 0, "native_start": 0, "frames": 5,
                                      "alignment_evidence": PROOF}]
        with self.assertRaisesRegex(ValueError, "truncation"):
            self.compare()
        self.manifest["mix_spans"] = [{"reference_start": 0, "native_start": 0, "frames": 3,
                                      "alignment_evidence": PROOF}] * 2
        with self.assertRaisesRegex(ValueError, "overlap"):
            self.compare()
        self.manifest.pop("mix_spans")
        write_wav(self.base / "native.wav", self.pairs, rate=44100)
        with self.assertRaisesRegex(ValueError, "48000"):
            self.compare()


if __name__ == "__main__":
    unittest.main()
