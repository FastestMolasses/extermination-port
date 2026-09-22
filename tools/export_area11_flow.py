#!/usr/bin/env python3
"""Export AREA11 event trigger geometry from the user's extracted overlay.

Run on native macOS/Linux/Windows Python; standard library only. MWo3 is
loaded whole at its header's load address, so no extra 0x40 is added to
runtime data addresses. Output is local disc-derived data, never committed.
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
TRIGGER_ADDRESSES = (0x0082ABE0, 0x0082AC20, 0x0082AC60)


def export_triggers(overlay: bytes) -> bytes:
    if len(overlay) != 0x7800 or overlay[:4] != b"MWo3":
        raise ValueError("Expected the SCUS-97112 AREA11 MWo3 overlay")
    load_address = struct.unpack_from("<I", overlay, 8)[0]
    if load_address != 0x00823500:
        raise ValueError("Unexpected AREA11 load address")
    output = bytearray(struct.pack("<4sII", b"EMAF", 1, 3))
    for address in TRIGGER_ADDRESSES:
        for vertex in range(4):
            offset = address - load_address + vertex * 16
            for component in (0, 8):  # the original plane-0 test uses X,Z
                bits = overlay[offset + component:offset + component + 4]
                if len(bits) != 4 or not math.isfinite(struct.unpack("<f", bits)[0]):
                    raise ValueError("Invalid AREA11 polygon coordinate")
                output += bits
    return bytes(output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--overlay", type=Path,
                        default=ROOT.parent / "Extermination/extract/OVERLAY/AREA11.BIN")
    parser.add_argument("--out", type=Path,
                        default=ROOT / "assets/scene_snow/area11_flow.emaf")
    args = parser.parse_args()
    result = export_triggers(args.overlay.read_bytes())
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(result)
    print(f"AREA11: exported three original trigger quadrilaterals to {args.out}")


if __name__ == "__main__":
    main()
