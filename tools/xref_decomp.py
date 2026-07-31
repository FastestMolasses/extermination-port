#!/usr/bin/env python3
"""Cross-reference this port's source against the sibling decomp's recovered C.

WHY THIS EXISTS. The port was written ahead of the decompilation, so much of it
is a structural sketch: the frame/task architecture is faithful, but individual
behaviours were filled in from observation and marked "placeholder", "approximate"
or "invented". Meanwhile the decomp has since recovered most of those same
functions as byte-matched C. This tool finds the overlap — the places where the
port is guessing about a function whose exact behaviour we now KNOW.

It reads only:
  * this repo's src/ (for func_XXXXXXXX / 0x00XXXXXX references and stub markers)
  * the sibling decomp's src/ headers and config/symbol_addrs.txt (for status)

It never copies disc-derived material; the decomp's src/*.c is our own C.

Status of a referenced PS2 function in the decomp:
  matched   — byte-identical C exists. Port it faithfully; this is ground truth.
  nearmiss  — readable C exists, body correct, not byte-identical. Still an
              excellent port source: the logic is right, only codegen differs.
  stub      — still undecompiled. The port cannot be made faithful here yet.
  absent    — not a function we have at all (bad reference, or data).

Usage:
  tools/xref_decomp.py                 # summary + priority list
  tools/xref_decomp.py --file F        # detail for one port file
  tools/xref_decomp.py --ready         # only refs whose decomp C is usable now
  tools/xref_decomp.py --json out.json # machine-readable, for wave input
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import defaultdict

PORT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DECOMP = os.path.join(os.path.dirname(PORT), "Extermination")

REF = re.compile(r'\b(func_[0-9A-Fa-f]{8})\b|\b(0x00[12][0-9A-Fa-f]{5})\b')
STUBMARK = re.compile(r'placeholder|approximat|invented|guess|TODO|FIXME|\bstub\b', re.I)


def decomp_status(name: str) -> str:
    p = os.path.join(DECOMP, "src", f"{name}.c")
    if not os.path.exists(p):
        return "absent"
    try:
        with open(p, errors="ignore") as f:
            head = f.read(200)
    except OSError:
        return "absent"
    if head.startswith("// NEARMISS"):
        return "nearmiss"
    if "INCLUDE_ASM" in head:
        return "stub"
    return "matched"


def addr_to_name() -> dict[str, str]:
    """Map 0xXXXXXXXX -> current decomp symbol name (semantic name wins)."""
    out: dict[str, str] = {}
    sa = os.path.join(DECOMP, "config", "symbol_addrs.txt")
    if os.path.exists(sa):
        for line in open(sa, errors="ignore"):
            m = re.match(r'\s*(\w+)\s*=\s*0x([0-9A-Fa-f]{8})\s*;.*type:func', line)
            if m:
                out[m.group(2).upper()] = m.group(1)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--file")
    ap.add_argument("--ready", action="store_true")
    ap.add_argument("--json")
    a = ap.parse_args()

    if not os.path.isdir(os.path.join(DECOMP, "src")):
        sys.exit(f"error: decomp repo not found at {DECOMP}")

    a2n = addr_to_name()

    # port file -> {ps2name: {status, count, near_stub_marker}}
    per_file: dict[str, dict[str, dict]] = defaultdict(dict)
    for root, _dirs, files in os.walk(os.path.join(PORT, "src")):
        for fn in files:
            if not fn.endswith((".c", ".h")):
                continue
            path = os.path.join(root, fn)
            rel = os.path.relpath(path, PORT)
            try:
                lines = open(path, errors="ignore").read().splitlines()
            except OSError:
                continue
            for i, line in enumerate(lines):
                for m in REF.finditer(line):
                    if m.group(1):
                        name = m.group(1)
                    else:
                        addr = m.group(2)[2:].upper()
                        name = a2n.get(addr, f"func_{addr}")
                    st = decomp_status(name)
                    # is this reference near a stub marker? (same line or +-3)
                    ctx = "\n".join(lines[max(0, i - 3): i + 4])
                    e = per_file[rel].setdefault(
                        name, {"status": st, "count": 0, "flagged": False, "lines": []})
                    e["count"] += 1
                    e["lines"].append(i + 1)
                    if STUBMARK.search(ctx):
                        e["flagged"] = True

    if a.file:
        sel = {k: v for k, v in per_file.items() if a.file in k}
    else:
        sel = per_file

    if a.json:
        json.dump(sel, open(a.json, "w"), indent=1)
        print(f"wrote {a.json}")

    # ---- summary
    tally: dict[str, int] = defaultdict(int)
    uniq: dict[str, str] = {}
    for refs in sel.values():
        for name, e in refs.items():
            uniq[name] = e["status"]
    for st in uniq.values():
        tally[st] += 1
    print(f"port files referencing PS2 functions: {len(sel)}")
    print(f"distinct PS2 functions referenced:    {len(uniq)}")
    for st in ("matched", "nearmiss", "stub", "absent"):
        print(f"    {st:9s} {tally[st]:4d}")

    usable = {n for n, s in uniq.items() if s in ("matched", "nearmiss")}
    print(f"\n  -> {len(usable)} referenced functions have usable decomp C today")

    # ---- priority: port files that GUESS about functions we now know exactly
    print("\n=== priority: port code flagged as placeholder/approximate whose")
    print("=== PS2 counterpart is already recovered in the decomp")
    rows = []
    for rel, refs in sel.items():
        ready = [(n, e) for n, e in refs.items()
                 if e["flagged"] and e["status"] in ("matched", "nearmiss")]
        if ready:
            rows.append((len(ready), rel, ready))
    for cnt, rel, ready in sorted(rows, reverse=True):
        print(f"\n  {rel}  ({cnt} fixable reference(s))")
        for n, e in sorted(ready, key=lambda kv: -kv[1]["count"])[:12]:
            ln = ",".join(str(x) for x in e["lines"][:4])
            print(f"      {n:28s} {e['status']:8s} x{e['count']:<3d} line {ln}")
    if not rows and a.ready:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
