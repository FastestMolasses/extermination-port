#!/usr/bin/env python3
"""Move a set of functions out of a large .c into a new module, safely.

WHY THIS EXISTS. em_game.c held the entire gameplay frame in ~11.8k lines. The
functions in it are NOT grouped by subsystem — camera, player, director and scene
code are interleaved — so the split cannot be done by cutting line ranges. This
moves functions by name: it finds each definition, takes its preceding comment
block with it, and rewrites both files.

What it handles that a manual cut does not:
  * a function's leading comment block travels with it (the port documents which
    PS2 function each part mirrors — losing that would destroy the provenance)
  * `static` is stripped from exported functions and kept on internal ones
  * forward declarations of moved functions are removed from the source file
  * the generated header declares exactly the exported set

What it deliberately does NOT do: guess at couplings. Callers that stay behind and
helpers that stay behind are found by the COMPILER, not by grep. Run make after
every split; the first two attempts here each surfaced a real coupling that a
caller survey had missed. Do not "fix" those by re-running with a wider set —
read the error and decide whether the symbol belongs in the module or in the
shared internal header.

Usage:
  split_module.py --src src/game/em_game.c --module em_camera \\
      --export camera_update,camera_solve --internal cam_norm3,cam_wrap_pi \\
      --title "camera system" [--dry-run]
"""
from __future__ import annotations

import argparse
import os
import re
import sys


def find_defs(lines: list[str], want: set[str]) -> dict[str, tuple[int, int]]:
    """name -> (first_idx, last_idx) covering the leading comment block + body."""
    out: dict[str, tuple[int, int]] = {}
    for idx, l in enumerate(lines):
        m = re.match(r'^(?:static\s+)?[A-Za-z_][\w \*]*?\b([A-Za-z_]\w*)\s*\(', l)
        if not m or m.group(1) not in want or m.group(1) in out:
            continue
        head = "\n".join(lines[idx:idx + 6])
        # skip forward declarations: '...);' before any '{'
        before_brace = head.split("{")[0]
        if ";" in before_brace and ")" in before_brace and "{" not in head[:len(before_brace)]:
            continue
        # walk back over the contiguous comment block
        i = idx
        while i > 0 and lines[i - 1].lstrip().startswith(("/*", "*", "*/", "//")):
            i -= 1
        # forward to the closing brace at depth 0
        j, depth, seen = idx, 0, False
        while j < len(lines):
            for ch in lines[j]:
                if ch == "{":
                    depth += 1
                    seen = True
                elif ch == "}":
                    depth -= 1
            if seen and depth == 0:
                break
            j += 1
        else:
            sys.exit(f"error: unterminated body for {m.group(1)}")
        out[m.group(1)] = (i, j)
    return out


def signature_of(lines: list[str], name: str, span: tuple[int, int]) -> str:
    a, b = span
    dl = next(i for i in range(a, b + 1)
              if re.match(r'^(?:static\s+)?[A-Za-z_][\w \*]*?\b' + re.escape(name) + r'\s*\(', lines[i]))
    parts, k = [], dl
    while "{" not in lines[k]:
        parts.append(lines[k])
        k += 1
    parts.append(lines[k].split("{")[0].rstrip())
    return re.sub(r'^static\s+', '', "\n".join(parts).strip()) + ";"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True)
    ap.add_argument("--module", required=True, help="basename, e.g. em_camera")
    ap.add_argument("--export", default="", help="comma-separated, part of the interface")
    ap.add_argument("--internal", default="", help="comma-separated, stay static")
    ap.add_argument("--title", default="")
    ap.add_argument("--blurb", default="")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    exported = [x for x in a.export.split(",") if x]
    internal = [x for x in a.internal.split(",") if x]
    want = set(exported) | set(internal)
    if not want:
        sys.exit("error: nothing to move")

    lines = open(a.src, errors="ignore").read().split("\n")
    found = find_defs(lines, want)
    missing = want - set(found)
    if missing:
        sys.exit(f"error: no definition found for: {', '.join(sorted(missing))}")

    blocks = sorted(found.items(), key=lambda kv: kv[1][0])
    moved: set[int] = set()
    for _n, (x, y) in blocks:
        moved.update(range(x, y + 1))

    body = []
    for n, (x, y) in blocks:
        text = "\n".join(lines[x:y + 1])
        if n in exported:
            text = re.sub(r'^static\s+', '', text, count=1, flags=re.M)
        body.append(text)

    decls = [signature_of(lines, n, found[n]) for n in exported]
    d = os.path.dirname(a.src)
    guard = a.module.upper() + "_H"
    title = a.title or a.module
    blurb = a.blurb or (
        "Split out of " + os.path.basename(a.src) + ", which had grown to hold the\n"
        " * entire gameplay frame. Behaviour is unchanged by the move — only the file\n"
        " * boundary is new.")

    hdr = (f"/* {a.module}.h — {title}.\n *\n * {blurb}\n */\n"
           f"#ifndef {guard}\n#define {guard}\n\n"
           f"/* The subsystem's shared types and state live here. */\n"
           f"#include \"game/em_game_internal.h\"\n\n"
           + "\n".join(decls) + f"\n\n#endif /* {guard} */\n")
    src = (f"/* {a.module}.c — {title}.\n *\n * {blurb}\n *\n"
           " * Every function here reads the shared gameplay state (EmGameState g), so\n"
           " * this module takes the subsystem's internal header rather than owning\n"
           " * private state — the same single state block the engine keeps in its\n"
           " * gameplay globals, now viewed from one more file. */\n\n"
           f"#include \"game/{a.module}.h\"\n\n#include \"game/em_game_internal.h\"\n\n"
           + "\n\n".join(body) + "\n")

    keep, i = [], 0
    fwd = re.compile(r'^static\s+[A-Za-z_][\w \*]*\b(' + "|".join(map(re.escape, exported)) + r')\s*\(')
    while i < len(lines):
        if i in moved:
            i += 1
            continue
        if exported and fwd.match(lines[i]):
            while i < len(lines) and ";" not in lines[i]:
                i += 1
            i += 1
            continue
        keep.append(lines[i])
        i += 1
    out_src = "\n".join(keep).replace(
        '#include "game/em_game_internal.h"',
        f'#include "game/em_game_internal.h"\n#include "game/{a.module}.h"', 1)

    print(f"moving {len(blocks)} function(s), {len(moved)} lines")
    for n, (x, y) in blocks:
        print(f"   {n:30s} lines {x+1}..{y+1}  {'[export]' if n in exported else '[static]'}")
    print(f"{a.src}: {len(lines)} -> {len(keep)} lines")
    if a.dry_run:
        print("(dry run — nothing written)")
        return 0

    open(os.path.join(d, a.module + ".h"), "w").write(hdr)
    open(os.path.join(d, a.module + ".c"), "w").write(src)
    open(a.src, "w").write(out_src)
    print(f"wrote {d}/{a.module}.c and {d}/{a.module}.h")
    print("NOW RUN make — couplings are found by the compiler, not by this script.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
