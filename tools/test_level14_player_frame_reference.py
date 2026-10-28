#!/usr/bin/env python3
"""Compare complete original 0015BCF0 with the bound translated native stage.

The original 0015BA50, skeleton evaluation, vitals, camera, footstep and stop
callees are scripted boundaries on both sides. Their bodies are not certified.
The real native frame, pose selector, stage tail and publication code run.
All 0x320 actor bytes, boundary entry snapshots, call order and published
scratch values are compared exactly. No original payload is written to disk.
Legacy unbound and bug-latch paths are outside this oracle's scope.
This is an opt-in owner-defect oracle, and returns failure on a mismatch.
"""
from __future__ import annotations

import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/later-oracle-player"
sys.path.insert(0, str(ROOT / "tools"))
from test_player_fall_reference import FallEE
from test_player_slide_reference import read_elf, s32
from test_player_floor_reference import NativeStage, StageWorkers, ADVANCE_TIME

ENTRY = 0x15BCF0
PLAYER = 0x8102B0
NODE = 0x1000000
BOUNDARIES = (0x15BA50, 0x1C6DA0, 0x1C68C0, 0x1C6960,
              0x15CF90, 0x15CBA0, 0x187350, 0x11A070)
OBSERVE = C.CFUNCTYPE(None, C.c_uint32, C.POINTER(C.c_uint8), C.c_int32)

class FrameEE(FallEE):
    def __init__(self, elf):
        super().__init__(elf)
        self.outcomes = set()
        self.entries = 0

    def execute(self, word, pc):
        if pc == ENTRY:
            self.entries += 1
        return super().execute(word, pc)

    def branch(self, word, pc):
        result = super().branch(word, pc)
        op, rs, rt = word >> 26, (word >> 21) & 31, (word >> 16) & 31
        unconditional = op in (4, 5, 20, 21) and rs == rt
        if ENTRY <= pc < 0x15BF90 and result is not None and not unconditional:
            self.outcomes.add((pc, bool(result[0])))
        return result


def difference(a, b):
    return [i for i, (x, y) in enumerate(zip(a, b)) if x != y]


def cases(rows):
    zero = next(i for i, value in enumerate(rows) if value == 0)
    nonzero = next(i for i, value in enumerate(rows) if value != 0)
    for mode in (0, 1, 2, 3, 4, 5):
        for clip in (zero, nonzero):
            for skip in (0, 1):
                yield dict(mode=mode, clip=clip, skip=skip, major=1,
                           state=0, y=2.0, event=0, live=0, handle=3, flag=0)
    for major in (1, 2, 6):
        for y in (-201.0, -200.0, -199.0):
            for event, state, flag in ((0x12E, 0, 0), (0x12E, 0x1C, 0),
                                       (0x135, 0, 0), (0x135, 0x17, 0),
                                       (0x5DD, 0x1D, 0), (0x5DD, 0x1D, 1),
                                       (0x5DD, 0x1E, 0), (0x5DD, 0x20, 0),
                                       (0x5DD, 0x21, 0)):
                for handle in (3, 255):
                    yield dict(mode=0, clip=zero, skip=0, major=major,
                               state=state, y=y, event=event, live=1,
                               handle=handle, flag=flag)
    for major in (1, 2, 6):
        for state in (0, 0x16, 0x17, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22):
            for event in (0, 0x12E, 0x135, 0x5DD):
                for flag in (0, 1, 2, 4):
                    yield dict(mode=0, clip=zero, skip=0, major=major,
                               state=state, y=2.0, event=event, live=1,
                               handle=3, flag=flag)


def actor(case):
    raw = bytearray(0x320)
    raw[4:7] = bytes((case["major"], case["state"], 0))
    struct.pack_into("<4f", raw, 0xA0, 1.25, case["y"], -3.5, 1.0)
    struct.pack_into("<4f", raw, 0xC0, 0.25, 0.5, -0.75, 1.0)
    struct.pack_into("<H", raw, 0x20C, case["clip"])
    struct.pack_into("<4f", raw, 0x220, 100.0, 0.0, 0.0, 0.0)
    raw[0x2F3], raw[0x303] = case["mode"], case["skip"]
    raw[0x31A], raw[0x31B], raw[0x275] = case["live"], case["handle"], case["flag"]
    struct.pack_into("<H", raw, 0x31C, case["event"])
    for i in range(22):
        struct.pack_into("<I", raw, 0x110 + i * 4, NODE + i * 0xD0)
    return raw


def compile_bridge(include_root, label, mutant):
    dylib = OUT / f"frame-{label}.dylib"
    command = ["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-ffp-contract=off", "-shared", "-fPIC"]
    if include_root:
        command += ["-I" + str(include_root)]
    if mutant:
        command += ["-DEM_TEST_FRAME_MUTANT=1"]
    command += ["-Isrc", "-Wl,-dead_strip",
                "-Wl,-exported_symbol,_lp_frame",
                "tests/level14_player_frame_bridge.c",
                "tests/level14_player_frame_unreachable.c",
                "src/game/em_player_floor.c", "src/game/em_player_record_pose.c",
                "-o", str(dylib)]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True)
    native = C.CDLL(str(dylib))
    native.lp_frame.argtypes = [
        C.POINTER(C.c_uint8), C.POINTER(C.c_int16), C.POINTER(C.c_float),
        C.c_uint32, C.c_int32,
        C.POINTER(StageWorkers), OBSERVE, C.POINTER(C.c_uint8),
        C.POINTER(C.c_uint32)]
    native.lp_frame.restype = C.c_int
    return native


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include-root", type=Path,
                        help="Private proposed-patch include tree; production files stay untouched.")
    parser.add_argument("--label", default="production")
    parser.add_argument("--mutant", action="store_true",
                        help="Corrupt the exported Euler buffer to check comparison sensitivity; no production source mutation.")
    args = parser.parse_args()
    assert args.label.replace("-", "").isalnum()
    OUT.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    elf = read_elf()
    # Certify the boundary set without printing or retaining original code.
    inspection = FrameEE(elf)
    direct_calls = {((inspection.load(pc) & 0x3FFFFFF) << 2)
                    for pc in range(ENTRY, 0x15BF90, 4)
                    if inspection.load(pc) >> 26 == 3}
    assert direct_calls == set(BOUNDARIES) | {0x102948}, "unexpected wrapper callee"
    for pc in range(ENTRY, 0x15BF90, 4):
        inspection.branch(inspection.load(pc), pc)
    conditional_sites = {pc for pc, _ in inspection.outcomes}
    required_outcomes = {(pc, outcome) for pc in conditional_sites for outcome in (False, True)}
    native = compile_bridge(args.include_root, args.label, args.mutant)
    rows = [struct.unpack_from("<h", elf, 0x248C90 - 0x100000 + 0x300 + i * 12)[0]
            for i in range(459)]
    row_array = (C.c_int16 * 459)(*rows)
    hip = struct.pack("<4f", 7.0, 8.0, 9.0, 1.0)
    hip_array = (C.c_float * 4).from_buffer_copy(hip)
    side = NativeStage(elf, {}, {ADVANCE_TIME: 0})
    failures = []
    passed = 0
    original_calls = set()
    branch_outcomes = set()
    entry_count = 0
    maximum_steps = 0
    paths = [dict(case, scripted=scripted, busy=busy, stage_busy=-1)
             for index, case in enumerate(cases(rows))
             for busy in ((0, 1) if index < 24 else (0,))
             for scripted in (False, True)]
    baseline = next(iter(cases(rows)))
    paths += [dict(baseline, scripted=scripted, busy=busy, stage_busy=1 - busy)
              for busy in (0, 1) for scripted in (False, True)]
    for index, case in enumerate(paths):
        raw = actor(case)
        original = FrameEE(elf)
        original.limit = 50000
        original.mem[PLAYER:PLAYER + len(raw)] = raw
        original.save(0x275B40, PLAYER + 0x110)
        original.save(0x700031F0, 0x12345678)
        original.save(0x8106B3, case["busy"], 1)
        original.mem[NODE + 0xD0 + 0xC0:NODE + 0xD0 + 0xD0] = hip
        expected_events = []

        def scripted_writes(address):
            if not case["scripted"]:
                return {}
            if address == 0x15BA50:
                return {0xB0: struct.pack("<f", -17.25),
                        0xC4: struct.pack("<f", 1.5)}
            if address in (0x1C6DA0, 0x1C68C0, 0x1C6960):
                return {0x2E0: b"\x17"}
            if address == 0x15CF90:
                return {0x220: struct.pack("<f", 73.0)}
            if address == 0x15CBA0:
                return {0x230: b"\x19"}
            if address == 0x187350:
                return {0x31B: b"\x04"}
            return {}

        def boundary(address):
            def run(ee):
                argument = s32(ee.r[4]) if address == 0x11A070 else 0
                assert address == 0x11A070 or (ee.r[4] & 0xFFFFFFFF) == PLAYER
                expected_events.append((address, argument, bytes(ee.mem[PLAYER:PLAYER + 0x320])))
                for offset, data in scripted_writes(address).items():
                    ee.mem[PLAYER + offset:PLAYER + offset + len(data)] = data
                if address == 0x15BA50 and case["stage_busy"] >= 0:
                    ee.save(0x8106B3, case["stage_busy"], 1)
                ee.r[2] = 0
            return run

        original.hooks = {address: boundary(address) for address in BOUNDARIES}
        original.call(ENTRY, (PLAYER,))
        expected = bytes(original.mem[PLAYER:PLAYER + 0x320])
        expected_publication = (
            original.load(0x700031F0),
            *struct.unpack("<4I", original.spad[0x3B40:0x3B50]),
            *struct.unpack("<4I", original.spad[0x3B50:0x3B60]),
            original.load(0x8106B3, 1), 0)
        actual_events = []

        @OBSERVE
        def observe(address, record, argument):
            actual_events.append((address, argument, C.string_at(record, 0x320)))
            for offset, data in scripted_writes(address).items():
                C.memmove(C.addressof(record.contents) + offset, data, len(data))

        out = (C.c_uint8 * 0x320)()
        published = (C.c_uint32 * 11)()
        status = native.lp_frame((C.c_uint8 * len(raw)).from_buffer_copy(raw),
                                 row_array, hip_array, case["busy"], case["stage_busy"],
                                 C.byref(side.workers),
                                 observe, out, published)
        original_calls.update(address for address, _, _ in expected_events)
        branch_outcomes.update(original.outcomes)
        entry_count += original.entries
        maximum_steps = max(maximum_steps, original.steps)
        errors = {}
        if status:
            errors["native_status"] = status
        wanted_order = [f"{address:08X}" for address, _, _ in expected_events]
        got_order = [f"{address:08X}" for address, _, _ in actual_events]
        if wanted_order != got_order:
            errors["call_order"] = {"original": wanted_order, "native": got_order}
        # Match entries by address and occurrence even when order is already wrong;
        # this reports independent record differences without forgiving the order.
        queues = {}
        for address, arg, snapshot in expected_events:
            queues.setdefault(address, []).append((arg, snapshot))
        for address, arg, snapshot in actual_events:
            if not queues.get(address):
                continue
            expected_arg, expected_snapshot = queues[address].pop(0)
            offsets = difference(expected_snapshot, snapshot)
            if offsets:
                errors.setdefault("boundary_record_offsets", {})[f"{address:08X}"] = offsets
            if arg != expected_arg:
                errors.setdefault("boundary_argument_mismatch", []).append(f"{address:08X}")
        offsets = difference(expected, bytes(out))
        if offsets:
            errors["final_record_offsets"] = offsets
        slots = difference(expected_publication, tuple(published))
        if slots:
            errors["publication_slots"] = slots
        if errors:
            failures.append({"case": index, "path": case, "mismatches": errors})
        else:
            passed += 1
    total = passed + len(failures)
    assert entry_count == total, "original wrapper entry did not execute once per case"
    assert branch_outcomes == required_outcomes, "wrapper conditional coverage is incomplete"
    report = {
        "schema": "later-player-full-frame-oracle-v1",
        "entry": "0015BCF0", "status": "PASS" if not failures else "FAIL",
        "cases": total, "passed": passed, "failed": len(failures),
        "original_body_execution_count": entry_count,
        "observed_branch_outcomes": len(branch_outcomes),
        "required_branch_outcomes": len(required_outcomes),
        "branch_sites_with_one_outcome": [
            f"{pc:08X}" for pc in sorted({pc for pc, _ in branch_outcomes})
            if (pc, False) not in branch_outcomes or (pc, True) not in branch_outcomes],
        "boundaries_observed": sorted(f"{x:08X}" for x in original_calls),
        "policy": "Exact call order, boundary arguments and whole 0x320-byte actor entry/final snapshots; exact carry, hip and Euler publication.",
        "scope": "Complete original wrapper against the bound translated native stage path. Stage, skeleton, vitals, camera, footstep and stop bodies are scripted boundaries. No legacy-path, later-level route replay or callee-body equivalence claim.",
        "input_constraints": "Finite record matrices, AC=1, live stage binding; two valid clip IDs sample both table selectors; six pose modes, skip, kill-plane equality, loop-stop classes, no-op and scripted callee writes; stage busy transitions 0-to-1 and 1-to-0.",
        "native_source_sha256": {
            source: hashlib.sha256((args.include_root / "game" / Path(source).name
                                    if args.include_root and (args.include_root / "game" / Path(source).name).exists()
                                    else ROOT / source).read_bytes()).hexdigest()
            for source in ("src/game/em_player.c", "src/game/em_player_frame.c",
                           "src/game/em_player_pose_host.c", "src/game/em_player_floor.c",
                           "src/game/em_player_record_pose.c")},
        "proposed_patch_test": bool(args.include_root),
        "deliberate_publication_mutant": args.mutant,
        "control_scope": "The optional mutant corrupts only the exported result buffer to check comparison sensitivity; it does not mutate production code.",
        "instruction_bound_per_case": 50000,
        "maximum_executed_instructions": maximum_steps,
        "pinned_original_elf_sha256": hashlib.sha256(elf).hexdigest(),
        "harness_sha256": {
            name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
            for name in ("tools/test_level14_player_frame_reference.py",
                         "tests/level14_player_frame_bridge.c",
                         "tests/level14_player_frame_unreachable.c")},
        "seconds": round(time.monotonic() - started, 3),
        "failures": failures,
    }
    path = OUT / f"frame-{args.label}.json"
    path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in
                      ("entry", "status", "cases", "passed", "failed", "seconds")}))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
