#!/usr/bin/env python3
"""Expected-failure check for the strict 0015BCF0 full-frame oracle.

tools/test_level14_player_frame_reference.py compares the complete original
player wrapper with the live production owner and exits nonzero on any
mismatch. The production owner is known to differ (docs/LEVEL_ORACLE_AUDIT.md,
"Requests for main: protected player wrapper"), so that strict run fails by
design until main applies the recorded proposal.

This check runs the unchanged strict oracle against the production owner and
passes only while it reproduces exactly the recorded failure signature:

* the comparison completed (the report is freshly written; harness asserts,
  build errors and crashes are failures here, not "expected"), all 1,384 cases
  ran, the original body was entered once per case and 44/44 conditional
  outcomes were covered;
* no case matches (a partly or fully fixed owner fails this check: retire it
  and point test-level14-player-frame-reference back at the strict oracle);
* the mismatch signature is the pinned one, in two layers: the tables below
  (mismatch key sets, call orders, final-record offset sets, publication slot
  sets, boundary record offsets and boundary argument mismatches, each with
  its case count), and a SHA-256 over every case's (index, inputs,
  mismatches). Any added, removed or moved difference - a new regression in
  the owner included - fails the check and prints which signatures changed.

The pinned values are call addresses and record offsets measured from the
oracle's own report; nothing disc-derived. No assertion of the strict oracle
is relaxed; its own target is test-level14-player-frame-strict.

Sensitivity controls (run after the check unless --no-controls):

* regression: a private include root (build/later-oracle-player/
  regression-control) holds a copy of the production em_player_frame.c that
  additionally flips record byte 0x10 in em_player_0015BCF0; the check must
  reject it because the final-record signature changed;
* proposal: when the private proposal tree build/later-oracle-player/
  proposed-include exists, the oracle passes there and the check must reject
  it as "now matches" (skipped, with a note, when the tree is absent).

The controls' reports, libraries and include copy are removed afterwards.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/later-oracle-player"
ORACLE = ROOT / "tools/test_level14_player_frame_reference.py"
LABEL = "expected-failure"
CASES = 1384
OUTCOMES = 44

# --- Pinned failure signature of the unfixed production owner -------------
# Measured 2026-10-20 from the strict oracle's report (identical in two runs).
KEY_SETS = {
    ("boundary_record_offsets", "call_order", "final_record_offsets", "publication_slots"): 710,
    ("boundary_argument_mismatch", "boundary_record_offsets", "call_order",
     "final_record_offsets", "publication_slots"): 425,
    ("call_order", "final_record_offsets", "publication_slots"): 249,
}
_A, _B, _C, _D, _E = "0015BA50", "001C68C0", "0015CF90", "0015CBA0", "00187350"
_V = "0011A070"
CALL_ORDERS = [  # (cases, original order, native order)
    (850, [_A, _B, _C, _D, _E, _V], [_A, _V, _D, _B, _E, _C]),
    (397, [_A, _B, _C, _D, _E], [_A, _D, _B, _E, _C]),
    (77, [_A, _B, _C, _D, _E, _V], [_A, _D, _B, _E, _C]),
    (48, [_A, "001C6960", _C, _D, _E], [_A, _D, "001C6960", _E, _C]),
    (8, [_A, _C, _D, _E], [_A, _D, _E, _C]),
    (4, [_A, "001C6DA0", _C, _D, _E], [_A, _D, "001C6DA0", _E, _C]),
]
FINAL_RECORD_OFFSETS = [
    (712, [178, 179, 183, 186, 187]),
    (348, [178, 179, 183, 186, 187, 795]),
    (170, [178, 179, 182, 183, 186, 187]),
    (77, [178, 179, 182, 183, 186, 187, 794, 795]),
    (77, [178, 179, 182, 183, 186, 187, 795]),
]
PUBLICATION_SLOTS = [(1334, [5, 7]), (50, [5, 7, 9])]
BOUNDARY_ARGUMENT_MISMATCH = [(425, [_V])]
_LATE = {_V: [546, 560, 736, 795]}
BOUNDARY_RECORD_OFFSETS = [
    (407, {**_LATE, _D: [546, 736, 794, 795], _C: [162, 163, 560, 794, 795],
           _E: [546, 794, 795], _B: [560, 794, 795]}),
    (407, {_D: [794, 795], _C: [794, 795], _E: [794, 795], _B: [794, 795]}),
    (219, {_D: [546, 736], _C: [162, 163, 560, 795], _E: [546], _B: [560]}),
    (24, {_D: [546, 736], _C: [162, 163, 560, 795], _E: [546], "001C6960": [560]}),
    (14, {**_LATE, _D: [4, 5, 546, 736, 794, 795], _C: [4, 5, 162, 163, 560, 794, 795],
          _E: [4, 5, 546, 794, 795], _B: [4, 5, 560, 794, 795]}),
    (14, {_D: [4, 5, 546, 736], _C: [4, 5, 162, 163, 560, 795], _E: [4, 5, 546], _B: [4, 5, 560]}),
    (14, {_D: [4, 5, 794, 795], _C: [4, 5, 794, 795], _E: [4, 5, 794, 795], _B: [4, 5, 794, 795]}),
    (14, {_D: [4, 5], _C: [4, 5], _E: [4, 5], _B: [4, 5]}),
    (4, {**_LATE, _D: [4, 546, 736, 794, 795], _C: [4, 162, 163, 560, 794, 795],
         _E: [4, 546, 794, 795], _B: [4, 560, 794, 795]}),
    (4, {_D: [4, 546, 736], _C: [4, 162, 163, 560, 795], _E: [4, 546], _B: [4, 560]}),
    (4, {_D: [4, 794, 795], _C: [4, 794, 795], _E: [4, 794, 795], _B: [4, 794, 795]}),
    (4, {_D: [4], _C: [4], _E: [4], _B: [4]}),
    (4, {_D: [546], _C: [162, 163, 560, 795], _E: [546]}),
    (2, {_D: [546, 736], _C: [162, 163, 560, 795], _E: [546], "001C6DA0": [560]}),
]
# SHA-256 of the canonical JSON list [[case, path, mismatches], ...] by case.
CASE_DIGEST = "5aca27d7874b48ba8e669966eeb781977310665c70d8c90ee658118d2c1a96ae"


def canon(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def pinned_tables():
    return {
        "key sets": {canon(sorted(k)): n for k, n in KEY_SETS.items()},
        "call_order": {canon({"original": o, "native": v}): n for n, o, v in CALL_ORDERS},
        "final_record_offsets": {canon(s): n for n, s in FINAL_RECORD_OFFSETS},
        "publication_slots": {canon(s): n for n, s in PUBLICATION_SLOTS},
        "boundary_record_offsets": {canon(s): n for n, s in BOUNDARY_RECORD_OFFSETS},
        "boundary_argument_mismatch": {canon(s): n for n, s in BOUNDARY_ARGUMENT_MISMATCH},
    }


def observed_tables(failures):
    tables = {"key sets": collections.Counter(canon(sorted(f["mismatches"])) for f in failures)}
    for key in ("call_order", "final_record_offsets", "publication_slots",
                "boundary_record_offsets", "boundary_argument_mismatch"):
        tables[key] = collections.Counter(canon(f["mismatches"][key])
                                          for f in failures if key in f["mismatches"])
    return {name: dict(table) for name, table in tables.items()}


def case_digest(failures):
    rows = [[f["case"], f["path"], f["mismatches"]]
            for f in sorted(failures, key=lambda f: f["case"])]
    return hashlib.sha256(canon(rows).encode()).hexdigest()


def run_oracle(label, include_root=None):
    """Run the strict oracle; return (exit code, fresh report or None)."""
    report = OUT / f"frame-{label}.json"
    if report.is_file() and not report.is_symlink():
        report.unlink()
    extra = ["--include-root", str(include_root)] if include_root else []
    run = subprocess.run([sys.executable, str(ORACLE), "--label", label, *extra], cwd=ROOT)
    return run.returncode, (json.loads(report.read_text()) if report.is_file() else None)


def check(returncode, report, production=True):
    """Return (verdict, messages): 'pass', 'matches' or 'signature'.

    Raises SystemExit on an incomplete comparison (a harness failure)."""
    if report is None:
        raise SystemExit(f"FAIL the strict oracle did not complete (exit {returncode}); "
                         "this is a harness failure, not the expected owner defect")
    if returncode == 0 or report["status"] == "PASS" or report["passed"]:
        return "matches", [f"the 0015BCF0 owner now matches in {report['passed']} of "
                           f"{report['cases']} cases"]
    assert returncode == 1, returncode
    assert report["status"] == "FAIL", report["status"]
    assert report["cases"] == CASES and report["failed"] == CASES, (report["cases"], report["failed"])
    assert report["original_body_execution_count"] == CASES, report["original_body_execution_count"]
    assert report["observed_branch_outcomes"] == OUTCOMES == report["required_branch_outcomes"], (
        report["observed_branch_outcomes"], report["required_branch_outcomes"])
    assert not report["deliberate_publication_mutant"]
    assert report["proposed_patch_test"] is (not production)
    failures = report["failures"]
    assert sorted(f["case"] for f in failures) == list(range(CASES)), "failure case indices"
    messages = []
    want, got = pinned_tables(), observed_tables(failures)
    for name in want:
        for signature in sorted(set(want[name]) | set(got[name])):
            pinned, seen = want[name].get(signature, 0), got[name].get(signature, 0)
            if pinned != seen:
                messages.append(f"{name}: {signature} pinned {pinned} cases, observed {seen}")
    digest = case_digest(failures)
    if digest != CASE_DIGEST:
        messages.append(f"per-case digest {digest} != pinned {CASE_DIGEST}"
                        + ("" if messages else " (same signature counts, different cases)"))
    return ("signature" if messages else "pass"), messages


def remove_outputs(label):
    for path in (OUT / f"frame-{label}.json", OUT / f"frame-{label}.dylib"):
        if path.is_file() and not path.is_symlink() and path.parent == OUT:
            path.unlink()


def regression_control():
    root = OUT / "regression-control"
    target = root / "game" / "em_player_frame.c"
    source = (ROOT / "src/game/em_player_frame.c").read_text()
    anchor = "    return em_glue_0015CF90(record->bytes, &vitals);\n"
    assert source.count(anchor) == 1, "regression control anchor not found once"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(source.replace(
        anchor, "    ((uint8_t *)record->bytes)[0x10] ^= 0x01u; /* control regression */\n" + anchor))
    label = f"{LABEL}-regression"
    try:
        verdict, messages = check(*run_oracle(label, root), production=False)
    finally:
        remove_outputs(label)
        if target.is_file() and not target.is_symlink():
            target.unlink()
        for folder in (root / "game", root):
            if folder.is_dir() and not folder.is_symlink() and not any(folder.iterdir()):
                folder.rmdir()
    if verdict != "signature" or not any(m.startswith("final_record_offsets:") for m in messages):
        raise SystemExit(f"FAIL regression control: an injected owner regression was not "
                         f"rejected as a signature change (verdict {verdict})")
    print(f"PASS regression control: flipping record byte 0x10 changes {len(messages)} pinned "
          "signature entries and is rejected")


def proposal_control():
    root = OUT / "proposed-include"
    if not (root / "game").is_dir():
        print(f"SKIP proposal control: private tree {root.relative_to(ROOT)} is absent")
        return
    label = f"{LABEL}-proposal"
    try:
        verdict, _ = check(*run_oracle(label, root), production=False)
    finally:
        remove_outputs(label)
    if verdict != "matches":
        raise SystemExit(f"FAIL proposal control: the proposed owner was not rejected as "
                         f"matching (verdict {verdict})")
    print("PASS proposal control: the proposed owner matches and is rejected as 'now matches'")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--no-controls", action="store_true",
                        help="Run only the production check, not the sensitivity controls.")
    args = parser.parse_args()
    verdict, messages = check(*run_oracle(LABEL))
    if verdict == "matches":
        raise SystemExit(f"FAIL {messages[0]}: retire this expected-failure check and make "
                         "test-level14-player-frame-reference run the strict oracle")
    if verdict == "signature":
        for message in messages:
            print("  " + message, file=sys.stderr)
        raise SystemExit("FAIL the strict 0015BCF0 oracle's failure signature changed from the "
                         "recorded defect (new owner regression or partial fix): see above")
    print(f"PASS expected failure: the strict 0015BCF0 oracle rejects the unfixed production "
          f"owner in all {CASES} cases ({OUTCOMES}/{OUTCOMES} outcomes) with exactly the pinned "
          "signature (tables + per-case digest)")
    if not args.no_controls:
        regression_control()
        proposal_control()


if __name__ == "__main__":
    main()
