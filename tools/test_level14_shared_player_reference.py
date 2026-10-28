#!/usr/bin/env python3
"""Audit shared player entries reached by the later-level readiness census.

Run the existing original-instruction suites without changing their cases,
comparisons, branch assertions or fault checks. An observation-only interpreter
hook records which census entries actually execute; a worker hook alone cannot
produce an entry hit. Builds and receipts live in build/later-oracle-player.

The 0015BCF0 stage-position suite checks only its feet-copy/order slice. This
runner explicitly retains that limitation; it does not certify the full frame
wrapper or a later-level route replay. All other selected entries execute in
the existing closure, ladder, major2 or healing suites.
"""
from __future__ import annotations

import argparse
import contextlib
import hashlib
import importlib
import json
import os
from pathlib import Path
import random
import sys
import time
import traceback

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/later-oracle-player"
SUITES = {
    "closure": (
        "test_player_closure_10_12_19_reference",
        (0x1696A0, 0x169730, 0x16A4B0, 0x16A8B0, 0x16ADE0, 0x16AE40,
         0x175390, 0x1787B0, 0x1811F0, 0x181430, 0x1814E0, 0x181730,
         0x1818D0, 0x181950, 0x181A70, 0x181B80, 0x181BA0, 0x181E20,
         0x181F60, 0x182090, 0x182100),
        ("src/game/em_player_closure_10_12_19.c", "src/game/em_player_fall.c",
         "src/game/em_player_major2.c"),
    ),
    "ladder": (
        "test_player_ladder_climb_reference",
        (0x17FD80, 0x17FE00, 0x17FF00, 0x180600, 0x1809B0),
        ("src/game/em_player_ladder_climb.c", "src/game/em_player_major2.c"),
    ),
    "major2": (
        "test_player_major2_reference", (0x181D70, 0x2230A0),
        ("src/game/em_player_major2.c", "src/game/em_player_floor.c"),
    ),
    "heal": (
        "test_player_heal_reference", (0x15C700,),
        ("src/game/em_player_stage_workers.c",),
    ),
    "frame_slice": (
        "test_player_stage_position_reference", (0x15BCF0,),
        ("src/game/em_player.c",),
    ),
    "frame_tail": (
        "test_player_floor_reference", (0x15BCF0,),
        ("src/game/em_player_floor.c",),
    ),
}
_ENTRIES = set()
_ENTRY_COUNTS = {}
_TOTAL = {}
_WORKER = None
_PARALLEL_MAP = None


class PrivateBuildRoot(type(Path())):
    """Keep the suite's source root while relocating only build destinations."""

    def __truediv__(self, child):
        name = str(child)
        if name == "build":
            return OUT / "build"
        if name.startswith("build/"):
            return OUT / "build" / name[len("build/"):]
        return super().__truediv__(child)


def _recorded_case(item):
    global _ENTRY_COUNTS
    _ENTRY_COUNTS = {}
    result = _WORKER(item)
    return result, _ENTRY_COUNTS


def _parallel_map(function, items, cost=None):
    global _WORKER, _ENTRY_COUNTS
    _WORKER = function
    parent_counts = _ENTRY_COUNTS
    results = _PARALLEL_MAP(_recorded_case, items, cost=cost)
    _ENTRY_COUNTS = parent_counts
    for _, counts in results:
        for address, count in counts.items():
            _TOTAL[address] = _TOTAL.get(address, 0) + count
    return [result for result, _ in results]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_suite(name):
    global _ENTRIES, _ENTRY_COUNTS, _TOTAL, _PARALLEL_MAP
    module_name, entries, sources = SUITES[name]
    _ENTRIES, _ENTRY_COUNTS, _TOTAL = set(entries), {}, {}
    suite = importlib.import_module(module_name)
    mode = importlib.import_module("reference_mode")
    interpreter = importlib.import_module("test_player_slide_reference")
    suite.ROOT = PrivateBuildRoot(ROOT)
    if name == "frame_slice":
        suite.OUT = OUT / "build/frame-slice"
    original_execute = interpreter.EE.execute

    def execute(self, word, pc):
        if pc in _ENTRIES:
            _ENTRY_COUNTS[pc] = _ENTRY_COUNTS.get(pc, 0) + 1
        return original_execute(self, word, pc)

    interpreter.EE.execute = execute
    _PARALLEL_MAP = mode.parallel_map
    mode.parallel_map = _parallel_map
    log_path = OUT / f"{name}-{mode.MODE}.log"
    started = time.monotonic()
    failure = None
    failure_trace = []
    try:
        with log_path.open("w") as log, contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
            if name == "frame_tail":
                elf = (suite.DECOMP / "config/SCUS_971.12").read_bytes()
                assert hashlib.sha256(elf).hexdigest() == suite.ELF_SHA256
                _, native = suite.build_native()
                result = {}
                suite.stage_section(elf, native, random.Random(0x1796C0), result)
                print(json.dumps(result, sort_keys=True))
                result = None
            else:
                result = suite.main()
            if result not in (None, 0):
                raise AssertionError("existing suite returned failure")
    except BaseException as error:
        failure = type(error).__name__
        failure_trace = [
            {"file": str(Path(frame.filename).name), "line": frame.lineno, "function": frame.name}
            for frame in traceback.extract_tb(error.__traceback__)
        ]
    finally:
        interpreter.EE.execute = original_execute
        mode.parallel_map = _PARALLEL_MAP
    for address, count in _ENTRY_COUNTS.items():
        _TOTAL[address] = _TOTAL.get(address, 0) + count
    missing = [f"{address:08X}" for address in entries if not _TOTAL.get(address)]
    report = {
        "schema": "later-player-original-oracle-v1",
        "suite": name,
        "existing_harness": f"tools/{module_name}.py",
        "mode": mode.MODE,
        "workers": int(os.environ["EM_TEST_JOBS"]),
        "status": "PASS" if failure is None and not missing else "FAIL",
        "failure_class": failure,
        "failure_trace": failure_trace,
        "missing_entry_execution": missing,
        "entry_execution_counts": {f"{address:08X}": _TOTAL.get(address, 0) for address in entries},
        "comparison_policy": "Existing cases, expected values, comparisons and coverage assertions unchanged.",
        "coverage_scope": (
            "Partial 0015BCF0 feet-copy/order and final feet publication only; full frame wrapper remains U."
            if name == "frame_slice" else (
            "Partial 0015BCF0 stage-tail writes and sound-stop behavior only; full frame wrapper remains U."
            if name == "frame_tail" else
            "Original bodies execute in the existing shared-player instruction oracle; no later-level route replay."
            )
        ),
        "harness_sha256": digest(ROOT / "tools" / f"{module_name}.py"),
        "native_sources_sha256": {source: digest(ROOT / source) for source in sources},
        "log": str(log_path.relative_to(ROOT)),
        "seconds": round(time.monotonic() - started, 3),
    }
    (OUT / f"{name}-{mode.MODE}.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, sort_keys=True), flush=True)
    return report["status"] == "PASS"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=tuple(SUITES), required=True)
    parser.add_argument("--full", action="store_true", help="Use the existing exhaustive sweep unchanged.")
    args = parser.parse_args()
    if args.full:
        os.environ["EM_TEST_FULL"] = "1"
    os.environ["EM_TEST_JOBS"] = str(min(2, max(1, int(os.environ.get("EM_TEST_JOBS", "2")))))
    os.environ.setdefault("EM_HEADLESS", "1")
    sys.path.insert(0, str(ROOT / "tools"))
    OUT.mkdir(parents=True, exist_ok=True)
    return 0 if run_suite(args.suite) else 1


if __name__ == "__main__":
    raise SystemExit(main())
