#!/usr/bin/env python3
"""G6: the canonical scene bytes have no shadow storage (SCENE_COORDINATOR_DESIGN.md
section 3.2; verification of step S11a).

Design 3.2: each byte the coordinator owns exists once, in EmSceneState
(src/game/em_scene_state.h), reached through em_scene_state(); no module keeps a
copy, and the port writes a byte only in the ported counterparts of its original
writers. This test greps src/ and checks, for the bytes S11a made canonical
(scratchpad 0x70003B8D and 0x70003B91, and the input words D_00810E74/E70/E50) and
S11b (scratchpad 0x70003B92, lead decision D5):

  1. the retired port copy `frame_selector` (g.frame_selector, S11a) is gone from
     src/ and tests/;
  2. assignments to the canonical fields occur only in the files of their
     original writers' counterparts (WRITERS below);
  3. no file-scope EmSceneState exists outside em_scene_bindings.c (the one
     owner, design 3.1);
  4. no declaration outside em_scene_state.h is annotated as storage of 3B8D,
     3B91 or 3B92 unless it is listed in ALLOWED with the reason and the work package
     that removes it. The list must only shrink: an entry that no longer
     matches is reported as stale and fails.

Scope: a grep, not a proof. It catches the known shadow pattern (a field whose
comment names the original byte) and stray writers; later steps extend BYTES,
WRITERS and ALLOWED when they make more bytes canonical.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"

# Field -> files (relative to src/) that may assign it, with the original writer.
WRITERS = {
    "spad3B8D": {
        "game/em_scene_task.c": "001AFCF0 clears it at every area load",
        "game/em_opening_runtime.c": "001B82D0 ops 9..12 phase 0 (=2) and op 4 (=0)",
    },
    "spad3B91": {
        "game/em_scene_task.c": "001AFCF0 clears it at every area load",
        "game/em_scene_frame.c": "001AE6B0 promotes 1 -> 2 (0x1AE6E0)",
        "game/em_opening_runtime.c": "001B82D0 ops 9..12 phase 0 (=0), phase 3 (=1), op 4 (=0)",
    },
    "spad3B92": {
        "game/em_scene_task.c": "001AFCF0 clears it at every area load",
        "game/em_opening_runtime.c": "001B82D0 ops 9..12 phase 3 (=1, 0x1B874C) and op 4 (=0, 0x1B8940)",
    },
    "d810E74": {
        "game/em_frame.c": "em_frame_scene_input: step C (001B5940) in the original layout",
        "game/em_scene_classify.c": "Q1: the classifier's local view with SELECT withheld",
    },
    "d810E70": {
        "game/em_frame.c": "em_frame_scene_input: step C (001B5940) in the original layout",
    },
    "d810E50": {
        "game/em_frame.c": "em_frame_scene_input: 001B5F40's pad-state byte, 4",
    },
}

# Declarations whose comment names 3B8D/3B91 outside em_scene_state.h.
ALLOWED = [
    {"file": "game/em_frame_trace.h", "name": "selector",
     "reason": "trace record of the canonical 3B8D value sampled at the 0x1AE040 entry "
               "(instrumentation, never read back)",
     "removed_by": "permanent"},
    {"file": "game/em_script.h", "name": "skip_request",
     "reason": "the interpreter's per-tick view of canonical 3B91 (relabelled in S11b): "
               "em_opening_runtime publishes the canonical byte before each tick and writes "
               "both, never back; the unwired interaction hosts (em_interaction_frame/"
               "cinematic) still keep their own value",
     "removed_by": "WP-4 (interaction host publishes canonical 3B91)"},
    {"file": "game/em_interaction_frame.h", "name": "selector",
     "reason": "unwired interaction host's 3B8D field (never called on the live path)",
     "removed_by": "WP-4"},
    {"file": "game/em_interaction_frame.h", "name": "ready",
     "reason": "unwired interaction host's 3B92 field (never called on the live path)",
     "removed_by": "WP-4"},
    {"file": "game/em_interaction_scan.h", "name": "selector",
     "reason": "unwired interaction scan's 3B8D input (never called on the live path)",
     "removed_by": "WP-6"},
]

BYTE_TOKEN = re.compile(r"3B8D|3B91|3B92", re.IGNORECASE)
DECL = re.compile(
    r"^\s*(?:static\s+)?(?:const\s+)?(?:volatile\s+)?"
    r"(?:u?int(?:8|16|32|64)_t|unsigned(?:\s+(?:char|short|int))?|char|short|int|bool)\s+"
    r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?:,\s*[A-Za-z_]\w*\s*)*;\s*(?:/[*/](.*))?$")
ASSIGN = r"\b{}\s*(?:=(?!=)|\+\+|--|[-+|&^]=)"
FILE_SCOPE_STATE = re.compile(r"^(?:static\s+)?EmSceneState\s+\w+\s*(?:=|;)")


def sources(root):
    return sorted(p for p in root.rglob("*") if p.suffix in (".c", ".h", ".m"))


def main():
    failures = []
    files = sources(SRC)

    # 1. The retired copy.
    for p in files + sources(ROOT / "tests"):
        for n, line in enumerate(p.read_text(errors="replace").splitlines(), 1):
            if re.search(r"\bframe_selector\b", line):
                failures.append(f"{p.relative_to(ROOT)}:{n}: frame_selector (retired by S11a)")

    # 2. Writers.
    for field, allowed in WRITERS.items():
        pattern = re.compile(ASSIGN.format(re.escape(field)))
        for p in files:
            rel = p.relative_to(SRC).as_posix()
            for n, line in enumerate(p.read_text(errors="replace").splitlines(), 1):
                code = line.split("//")[0]
                if pattern.search(code) and rel not in allowed:
                    failures.append(f"src/{rel}:{n}: writes {field}; its writers are "
                                    f"{', '.join(sorted(allowed))}")

    # 3. One owner of the state.
    for p in files:
        rel = p.relative_to(SRC).as_posix()
        if rel == "game/em_scene_bindings.c":
            continue
        for n, line in enumerate(p.read_text(errors="replace").splitlines(), 1):
            if FILE_SCOPE_STATE.match(line):
                failures.append(f"src/{rel}:{n}: file-scope EmSceneState outside the bindings")

    # 4. Annotated shadow declarations.
    used = set()
    for p in files:
        rel = p.relative_to(SRC).as_posix()
        if rel == "game/em_scene_state.h":
            continue
        for n, line in enumerate(p.read_text(errors="replace").splitlines(), 1):
            m = DECL.match(line)
            if not m or not m.group(2) or not BYTE_TOKEN.search(m.group(2)):
                continue
            hit = [i for i, a in enumerate(ALLOWED) if a["file"] == rel and a["name"] == m.group(1)]
            if hit:
                used.update(hit)
            else:
                failures.append(f"src/{rel}:{n}: `{m.group(1)}` is declared as storage of "
                                f"{BYTE_TOKEN.search(m.group(2)).group(0)}; use em_scene_state()")
    for i, a in enumerate(ALLOWED):
        if i not in used:
            failures.append(f"stale ALLOWED entry {a['file']}:{a['name']} (remove it)")

    if failures:
        print("scene no-shadow (G6): FAIL")
        for f in failures:
            print("  " + f)
        return 1
    print(f"scene no-shadow (G6): PASS ({len(files)} files; {len(WRITERS)} canonical fields "
          f"with original writer sets; {len(ALLOWED)} listed non-canonical declarations)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
