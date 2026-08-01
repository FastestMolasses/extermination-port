#!/usr/bin/env python3
"""Extract the port's confidence CLAIMS so each can be checked against the decomp.

WHY THIS EXISTS. The port's comments are full of assertions — "DECODED", "VERIFIED",
"CONFIRMED", "LIVE-VERIFIED" — most written before the decompilation recovered the
functions they cite. In the decomp repo, the same generation of sessions produced
seven "compiler walls" that were really defects in our own inputs, and three
"proven impossible" verdicts that were false. There is no reason to think the
port's assertions are more reliable than those were.

So the audit surface is NOT the code marked "placeholder" — those are honestly
flagged. It is the code marked DECODED/VERIFIED, which everyone downstream trusts.

This pairs every claim with the PS2 function(s) named nearby and that function's
real status in the decomp, giving three buckets:

  CHECKABLE  the claim cites a function whose C we have recovered. Verify the
             claim against it — this is where wrong "decoded" constants hide.
  UNGROUNDED the claim cites a function that is still an undecompiled stub (or
             cites nothing at all). It could not have been decoded from source,
             so it rests on observation. Downgrade the wording; do not trust it.
  DATA       the claim cites an address that is not a function (a table/global).

Usage:
  audit_claims.py                      # summary by file and bucket
  audit_claims.py --file em_enemy.c    # every claim in one file
  audit_claims.py --json out.json      # machine-readable, for wave input
  audit_claims.py --ungrounded         # only the claims with no recovered source
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

CLAIM = re.compile(r'\b(DECODED|VERIFIED|CONFIRMED|LIVE-VERIFIED)\b', re.I)
FUNC = re.compile(r'\b(func_[0-9A-Fa-f]{8})\b|\b(0x00[12][0-9A-Fa-f]{5})\b')


def decomp_status(name: str) -> str:
    p = os.path.join(DECOMP, "src", f"{name}.c")
    if not os.path.exists(p):
        return "absent"
    try:
        head = open(p, errors="ignore").read(200)
    except OSError:
        return "absent"
    if head.startswith("// NEARMISS"):
        return "nearmiss"
    return "stub" if "INCLUDE_ASM" in head else "matched"


def addr_map() -> dict[str, str]:
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
    ap.add_argument("--json")
    ap.add_argument("--ungrounded", action="store_true")
    ap.add_argument("--unaudited", action="store_true",
                    help="only claims with no audit stamp in their context")
    ap.add_argument("--stamp", default="2026-07-31",
                    help="audit stamp that marks a claim as already verified")
    ap.add_argument("--context", type=int, default=6,
                    help="comment lines around the claim to carry along")
    a = ap.parse_args()

    a2n = addr_map()
    per_file: dict[str, list[dict]] = defaultdict(list)

    for root, _d, files in os.walk(os.path.join(PORT, "src")):
        for fn in sorted(files):
            if not fn.endswith((".c", ".h")):
                continue
            path = os.path.join(root, fn)
            rel = os.path.relpath(path, PORT)
            if a.file and a.file not in rel:
                continue
            lines = open(path, errors="ignore").read().splitlines()
            for i, line in enumerate(lines):
                if not CLAIM.search(line):
                    continue
                lo, hi = max(0, i - a.context), min(len(lines), i + a.context + 1)
                ctx = "\n".join(lines[lo:hi])
                cited = []
                for m in FUNC.finditer(ctx):
                    if m.group(1):
                        nm = m.group(1)
                    else:
                        nm = a2n.get(m.group(2)[2:].upper(), "")
                        if not nm:
                            cited.append(("(data/addr) " + m.group(2), "data"))
                            continue
                    cited.append((nm, decomp_status(nm)))
                seen, uniq = set(), []
                for nm, st in cited:
                    if nm not in seen:
                        seen.add(nm)
                        uniq.append({"name": nm, "status": st})
                usable = [c for c in uniq if c["status"] in ("matched", "nearmiss")]
                bucket = ("CHECKABLE" if usable
                          else "DATA" if uniq and all(c["status"] == "data" for c in uniq)
                          else "UNGROUNDED")
                audited = a.stamp in ctx or "AUDIT CORRECTION" in ctx
                if a.unaudited and audited:
                    continue
                per_file[rel].append({
                    "audited": audited,
                    "line": i + 1,
                    "claim": CLAIM.search(line).group(1).upper(),
                    "bucket": bucket,
                    "cites": uniq,
                    "text": line.strip()[:200],
                })

    if a.json:
        json.dump(per_file, open(a.json, "w"), indent=1)
        print(f"wrote {a.json}")

    tally: dict[str, int] = defaultdict(int)
    for rows in per_file.values():
        for r in rows:
            tally[r["bucket"]] += 1
    total = sum(tally.values())
    aud = sum(1 for rows in per_file.values() for r in rows if r.get("audited"))
    print(f"claims found: {total}   (already audited: {aud})")
    for b in ("CHECKABLE", "UNGROUNDED", "DATA"):
        print(f"    {b:11s} {tally[b]:4d}")
    print("\nCHECKABLE = cites a function we have recovered -> verify the claim against it")
    print("UNGROUNDED = cites nothing recoverable -> it rests on observation, not source\n")

    rows = sorted(per_file.items(),
                  key=lambda kv: -sum(1 for r in kv[1] if r["bucket"] == "CHECKABLE"))
    for rel, items in rows:
        c = sum(1 for r in items if r["bucket"] == "CHECKABLE")
        u = sum(1 for r in items if r["bucket"] == "UNGROUNDED")
        if not items:
            continue
        print(f"  {rel:34s} {len(items):4d} claims   checkable {c:3d}   ungrounded {u:3d}")

    if a.file or a.ungrounded:
        print()
        for rel, items in rows:
            for r in items:
                if a.ungrounded and r["bucket"] != "UNGROUNDED":
                    continue
                cites = ", ".join(f"{c['name']}[{c['status']}]" for c in r["cites"]) or "-"
                print(f"  {rel}:{r['line']}  {r['claim']}  [{r['bucket']}]  {cites}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
