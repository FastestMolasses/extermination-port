#!/usr/bin/env python3
"""Normalize private original-initialization evidence for offline DSP stages.

Only public register addresses (the PS2SDK register map) and the existing
original-instruction oracle are used. No guessed reset constants or emulator
implementation is involved. Words are recorded raw, by register name and
offset only: no permitted public source documents the bit layout of MMIX or
ATTR, the meaning of the external-input volumes or the mixer order, so none of
them is decoded here (docs/SPU2_OUTPUT.md "Sources and clean-room boundary").
The asymmetric set-switch probe establishes mask halfword ordering using the
user's resident original LIBSD; raw register values remain private files.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from export_spu2_effects import capture_iop, mmio_cpu
from spu2_stage_compare import private_destination
from test_iop_stream_reference import DECOMP, DriverOracle, iso_file

MAP_SOURCE = "https://ps2dev.github.io/ps2sdk/spu2regs_8h.html"


def mask_order(oracle):
    tested = 0
    for core in (0, 1):
        for selector, offset in ((0x1800, 0x188), (0x1900, 0x18c),
                                 (0x1a00, 0x190), (0x1b00, 0x194)):
            cpu, registers, written = mmio_cpu(oracle)
            cpu.hooks.pop(oracle.base + 0x315c)
            mask = 0x123456
            cpu.call(oracle.base + 0x315c, (selector | core, mask))
            start = offset + core * 0x400
            if not set(range(start, start + 4)).issubset(written):
                raise ValueError("original switch setter did not write both mask halves")
            if struct.unpack_from("<I", registers, start)[0] != mask:
                raise ValueError("mask layout differs from low-halfword-first hypothesis")
            tested += 1
    return {"original_setter_cases": tested, "result": "PASS",
            "layout": "low 16 bits at lower address, high 8 bits at address+2"}


def normalize(registers, receipt):
    stable = set(receipt["stable_initialized_bytes"])
    if len(registers) != 2048 or receipt["status"] != "PASS":
        raise ValueError("initialization receipt/register page invalid")
    def half(offset):
        if not {offset, offset + 1} <= stable:
            return None
        return struct.unpack_from("<H", registers, offset)[0]
    def mask(offset):
        if not set(range(offset, offset + 4)) <= stable:
            return None
        return struct.unpack_from("<I", registers, offset)[0] & 0xffffff
    def address(offset):
        hi, lo = half(offset), half(offset + 2)
        return None if hi is None or lo is None else hi * 65536 + lo
    cores = []
    for core in (0, 1):
        switch, params, effect = core * 0x400, 0x760 + core * 40, 0x2e0 + core * 0x400
        cores.append({
            "core": core,
            "master": [half(params), half(params + 2)],
            "effect_return": [half(params + 4), half(params + 6)],
            # AVOL/BVOL, MMIX and ATTR by PS2SDK name: raw, not interpreted.
            "avol_raw": [half(params + 8), half(params + 10)],
            "bvol_raw": [half(params + 12), half(params + 14)],
            "mmix_raw": half(switch + 0x198),
            "attr_raw": half(switch + 0x19a),
            "pmon": mask(switch + 0x180), "non": mask(switch + 0x184),
            "dry_left": mask(switch + 0x188), "dry_right": mask(switch + 0x190),
            "wet_left": mask(switch + 0x18c), "wet_right": mask(switch + 0x194),
            "effect_start": address(effect),
            # EEA_HI by PS2SDK name, raw; its encoding is not decoded here.
            "effect_end_raw": half(effect + 0x5c),
        })
    return {"schema": "spu2-normalized-initial-state-v2", "cores": cores,
            "map_source": MAP_SOURCE,
            "source_scenario": ("conditional post-movie original master restore; not a live capture"
                                if receipt.get("master_restore") else
                                "original initialization only"),
            "unknown_policy": "null means not fully initialized in every oracle seed run",
            "limits": receipt["limits"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--initialization", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--iso", type=Path, default=DECOMP / "Extermination-rebuilt.iso")
    parser.add_argument("--iop", type=Path, default=DECOMP /
                        "build/startup-reference/fork-states/beats/s87/route/00_panel_no_battery/state.p2s")
    args = parser.parse_args()
    try:
        receipt_path = args.initialization / "receipt.json"
        register_path = args.initialization / "registers-00.bin"
        receipt = json.loads(receipt_path.read_text())
        _, module = iso_file(args.iso, "/IRX/SNDN2DRV.IRX")
        memory = capture_iop(args.iop)
        if hashlib.sha256(module).hexdigest() != receipt["driver_sha256"]:
            raise ValueError("driver provenance does not match initialization")
        if hashlib.sha256(memory).hexdigest() != receipt["resident_iop_sha256"]:
            raise ValueError("resident LIBSD provenance does not match initialization")
        oracle = DriverOracle(module, memory)
        result = normalize(register_path.read_bytes(), receipt)
        result["mask_order_probe"] = mask_order(oracle)
        result["initialization_receipt_sha256"] = hashlib.sha256(receipt_path.read_bytes()).hexdigest()
        result["register_page_sha256"] = hashlib.sha256(register_path.read_bytes()).hexdigest()
        out = private_destination(args.out)
        out.mkdir(parents=True, exist_ok=False)
        (out / "state.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps({"status": "PASS", "mask_setter_cases": 8, "cores": 2,
                          "state_sha256": hashlib.sha256((out / "state.json").read_bytes()).hexdigest()}))
        return 0
    except (OSError, ValueError, KeyError) as error:
        print("SPU2 state normalization failed: " + str(error))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
