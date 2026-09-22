#!/usr/bin/env python3
"""Export the local AREA11 opening program and its actor spawn list.

Output contains user-supplied disc data and must remain under ignored
assets/. The 64-byte script records retain their original addresses and
flags so the native interpreter can follow the original command stream.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x00828F30
ENTRY = 0x00828FC0
END = 0x008292C0


def export_opening(overlay: bytes) -> bytes:
    if len(overlay) != 0x7800 or overlay[:4] != b"MWo3":
        raise ValueError("Expected SCUS-97112 AREA11 overlay")
    arena = struct.unpack_from("<I", overlay, 8)[0]
    if arena != 0x00823500:
        raise ValueError("Unexpected AREA11 arena")
    payload = overlay[BASE-arena:END-arena]
    if len(payload) != END-BASE:
        raise ValueError("Truncated opening program")
    first = struct.unpack_from("<I",payload,ENTRY-BASE)[0]
    last = struct.unpack_from("<I",payload,END-BASE-64)[0]
    if first != 7 or last != 0x80000007:
        raise ValueError("Opening script bounds do not match the original program")
    return struct.pack("<4sIIII",b"EMSC",1,BASE,ENTRY,len(payload)) + payload


def main() -> None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--overlay",type=Path,
                        default=ROOT.parent/"Extermination/extract/OVERLAY/AREA11.BIN")
    parser.add_argument("--out",type=Path,
                        default=ROOT/"assets/scene_snow/opening.emsc")
    args=parser.parse_args()
    blob=export_opening(args.overlay.read_bytes())
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_bytes(blob)
    print(f"AREA11: exported opening script and actor list to {args.out}")


if __name__=="__main__": main()
