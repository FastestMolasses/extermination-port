#!/usr/bin/env python3
"""Record original master-setter semantics and the documented movie lifecycle.

Runs the existing original IOP instruction oracle on native macOS arm64.
Pure C definitions establish callers/arguments; original driver and resident
LIBSD bodies establish the resulting register writes. This is not a live
register capture and does not supply an exact applied-sample timestamp.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from export_spu2_effects import capture_iop, mmio_cpu
from test_iop_stream_reference import DECOMP, DriverOracle, iso_file

ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def definition(name):
    path = DECOMP / "src" / ("func_" + name + ".c")
    raw = path.read_bytes()
    text = raw.decode()
    if re.search(r"\b(?:asm|__asm__)\b", text):
        raise ValueError("master probe requires a pure C definition")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text), digest(raw)


def constant_master_calls(text):
    result = []
    for arguments in re.findall(r"func_00119978\(([^)]*)\)", text):
        try:
            values = [int(value.strip(), 0) for value in arguments.split(",")]
        except ValueError:
            continue
        if len(values) == 3:
            result.append([0x28, *values])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--iso", type=Path, default=DECOMP / "Extermination-rebuilt.iso")
    parser.add_argument("--iop", type=Path, default=DECOMP /
                        "build/startup-reference/fork-states/beats/s87/route/00_panel_no_battery/state.p2s")
    args = parser.parse_args()
    out = args.out.resolve()
    if not out.is_relative_to(ROOT / "build") or out.exists():
        parser.error("choose a fresh private JSON path under build/")
    definitions, hashes = {}, {}
    for name in ("00119978", "001AB7E0", "001F9BF0", "00203350", "002036E0"):
        definitions[name], hashes[name] = definition(name)
    assert re.search(r"func_001157F0\(0x28,\s*a0,\s*a1,\s*a2\)", definitions["00119978"])
    startup = constant_master_calls(definitions["001AB7E0"])
    restored = constant_master_calls(definitions["001F9BF0"])
    muted = constant_master_calls(definitions["002036E0"])
    assert startup == restored and len(restored) == 2
    assert [row[1] for row in restored] == [0, 1]
    assert muted == [[0x28, 0, 0, 0], [0x28, 1, 0, 0]]
    caller = definitions["00203350"]
    assert caller.index("func_001F9BF0();", caller.index("void func_00203350")) < \
        caller.index("D_00821058[0] = 0;")
    _, module = iso_file(args.iso, "/IRX/SNDN2DRV.IRX")
    memory = capture_iop(args.iop)
    oracle = DriverOracle(module, memory)
    cases = []
    for words in muted + restored + [[0x28, c, 0x1234, 0x2345] for c in (0, 1)]:
        cpu, registers, written = mmio_cpu(oracle)
        registers[:] = bytes([0x55]) * len(registers)
        pointer = oracle.base + 0x34b0
        cpu.write(pointer, struct.pack("<4I", *words))
        assert cpu.call(oracle.base + 0x634, (pointer, 16, 0)) == 0
        offset = 0x760 + words[1] * 40
        expected = list(range(offset, offset + 4))
        assert sorted(written) == expected
        values = list(struct.unpack_from("<HH", registers, offset))
        assert values == words[2:]
        cases.append({"command": words, "written_offsets": expected,
                      "master_words": values, "exact": True})
    result = {
        "schema": "spu2-original-master-restore-v1", "status": "PASS",
        "restore_commands": restored, "startup_commands": startup,
        "movie_tail_mute_commands": muted, "cases": cases,
        "lifecycle": [
            "001AB7E0 case5 queues both master restore commands before switching to the title task.",
            "002036E0 queues both zero-master commands during the movie tail.",
            "00203350 calls001F9BF0 after movie teardown and before clearing D_00821058.",
            "001F9BF0 queues both master restore commands and resets each effect mode to4.",
        ],
        "scope": "Caller order/arguments from pure C; actual command0x28 and residentLIBSD setter bodies executed.",
        "limits": [
            "No live master-register capture or exact applied-sample timestamp.",
            "Queued restore becomes active only after original RPC processing.",
            "Using restored values throughout post-movie gameplay is a conditional lifecycle scenario.",
            "This probe does not establish startup fade timing, RAM/FIR history or final output-block state.",
        ],
        "sha256": {"driver": digest(module), "resident_iop": digest(memory),
                   "definitions": hashes, "tool": digest(Path(__file__).read_bytes())},
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"status": "PASS", "cases": len(cases), "receipt": str(out),
                      "live_register_capture": False, "payload_printed": False}))


if __name__ == "__main__":
    main()
