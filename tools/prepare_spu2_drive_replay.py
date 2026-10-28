#!/usr/bin/env python3
"""Anchor measured original reads to the private A2 input-playback segments.

No payload is embedded or printed. This writes only a fresh ignored file.
The private observer preparer installs the matching a2segN input-step hook.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fresh(path):
    path = path.absolute()
    if (not path.is_relative_to(ROOT / "build") or ".." in path.parts
            or any(p.is_symlink() for p in (path, *path.parents))
            or path.exists()):
        raise ValueError("choose a fresh path inside this worktree's real build/")
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    output = fresh(args.out)
    receipt = fresh(output.with_suffix(".json"))
    if output == receipt:
        raise ValueError("replay and JSON receipt must have different filenames")
    source = args.reference.resolve()
    sys.path.insert(0, str(ROOT.parent / "Extermination/tools/video_compare"))
    import emrec
    recording, reads = source / "ps2.rec", source / "original.reads"
    first = {}
    for row in emrec.read(recording).ticks():
        first.setdefault(row["seg"], row)
    if not first or any(row["off"] != 0 for row in first.values()):
        raise ValueError("reference must begin at each included segment's offset zero")
    text = reads.read_text()
    count = 0
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        values = [int(x) for x in line.split()]
        if len(values) != 4 or any(x < 0 for x in values):
            raise ValueError("invalid measured read row")
        count += 1
    if not count:
        raise ValueError("reference has no measured reads")
    contents = "# measured original reads with playback segment counter anchors; private only\n"
    contents += "".join(f"@ a2seg{seg} {row['counter']}\n"
                        for seg, row in sorted(first.items()))
    contents += text
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(contents)
    data = {"schema": "spu2-drive-replay-preparation-v1", "anchors": len(first),
            "reads": count, "tool_sha256": digest(Path(__file__)),
            "inputs": {str(p): digest(p) for p in (recording, reads)},
            "output": str(output), "output_sha256": digest(output)}
    receipt.write_text(json.dumps(data, indent=2) + "\n")
    print(json.dumps({"anchors": len(first), "reads": count, "payload_printed": False}))


if __name__ == "__main__":
    main()
