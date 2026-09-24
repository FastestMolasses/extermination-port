#!/usr/bin/env python3
"""G6: the canonical scene bytes have no shadow storage (SCENE_COORDINATOR_DESIGN.md
section 3.2; verification of step S11a).

Design 3.2: each byte the coordinator owns exists once, in EmSceneState
(src/game/em_scene_state.h), reached through em_scene_state(); no module keeps a
copy, and the port writes a byte only in the ported counterparts of its original
writers. This test greps src/ and checks, for the bytes S11a made canonical
(scratchpad 0x70003B8D and 0x70003B91, and the input words D_00810E74/E70/E50),
S11b (scratchpad 0x70003B92, lead decision D5), the housekeeping step HK
(lead decision D2: D_00810707, D_00810792, D_00810793, D_00810813, D_00810CC3,
D_00810CB6 in the EmProgress region and D_008106F1 in the request block) and
census L01 (D_0081083C, the player's grab-slot bits, in the EmProgress region):

  1. the retired port copies are gone from src/ and tests/: `frame_selector`
     (g.frame_selector, S11a), `cine_step` (g.cine_step = D_00810813, HK) and
     `opening_key_item_zero` (g.opening_key_item_zero = D_00810CC3[0], HK);
  2. assignments to the canonical scratchpad/input fields occur only in the
     files of their original writers' counterparts (WRITERS below). A pointer
     view that writes through to the canonical byte counts as a writer and is
     listed with its original function. A per-call view whose field has the
     same name is loaded by an assignment that is not a canonical write: it is
     listed in VIEW_LOADS with the exact line pattern;
  3. no file-scope EmSceneState exists outside em_scene_bindings.c (the one
     owner, design 3.1);
  4. no declaration outside em_scene_state.h is annotated (in its comment or
     its name) as storage of one of the canonical bytes (BYTE_TOKEN) unless it is
     listed in ALLOWED with the reason and the step that removes it. Pointer
     declarations are not storage and never match;
  5. the code of only the files in REACHERS names a migrated progress/request
     byte's address (outside comments), with the original function each one
     stands for; callers of em_director_original_001C4760_scene (the live
     001C4760 binding) count as reaching D_00810CC3.
The lists must only shrink: an entry that no longer matches is reported as
stale and fails.

Scope: a grep, not a proof. It catches the known shadow pattern (a field whose
comment or name names the original byte) and stray writers; later steps extend
BYTE_TOKEN, WRITERS, REACHERS and ALLOWED when they make more bytes canonical.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"

# Field -> files (relative to src/) that may assign it, with the original writer.
WRITERS = {
    "spad3B8D": {
        "game/em_area_script.c": "001B82D0 (unbound, L19): the frame view store after each sub-handler "
                                 "(frame_store), its sub 6 teardown (=0), and 001B6E40 (op 0x16, =3 once "
                                 "00182BF0 reports the player free); a pointer view onto the canonical byte",
        "game/em_scene_task.c": "001AFCF0 clears it at every area load",
        "game/em_opening_runtime.c": "001B82D0 ops 9..12 phase 0 (=2) and op 4 (=0)",
        "game/em_scene_bindings.c": "001B0C60 area-change request (=3; S12a)",
        "game/em_area11_interaction_host.c": "WP-4: the host's frame view store (001B82D0 sub0/2/13 "
                                             "=2/1, sub4 =0; 00184BA0's winner claim =3) and "
                                             "002149F0's successful exit (=3)",
    },
    "spad3B91": {
        "game/em_area_script.c": "001B82D0 (unbound, L19): the skip byte its sub 6 teardown clears "
                                 "(skip_set) and the frame view store after each 001B82D0 sub-handler "
                                 "(frame_store writes the running script's skip byte back)",
        "game/em_scene_task.c": "001AFCF0 clears it at every area load",
        "game/em_scene_frame.c": "001AE6B0 promotes 1 -> 2 (0x1AE6E0)",
        "game/em_opening_runtime.c": "001B82D0 ops 9..12 phase 0 (=0), phase 3 (=1), op 4 (=0)",
        "game/em_area11_interaction_host.c": "WP-4: the running script's skip byte stored back after "
                                             "its owner tick (001B82D0 sub0/sub4 clear it)",
    },
    "spad3B92": {
        "game/em_area_script.c": "001B82D0 (unbound, L19): the frame view store (frame_store) and its "
                                 "sub 6 teardown (=0); a pointer view onto the canonical byte",
        "game/em_scene_task.c": "001AFCF0 clears it at every area load",
        "game/em_opening_runtime.c": "001B82D0 ops 9..12 phase 3 (=1, 0x1B874C) and op 4 (=0, 0x1B8940)",
        "game/em_area11_interaction_host.c": "WP-4: the host's frame view store (001B82D0 sub0/2/13 "
                                             "phase 1 =1, sub4 =0)",
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

# Assignments to a same-named per-call view field (not a canonical write): the
# file, the field, the exact line pattern, the reason and the step that removes it.
VIEW_LOADS = [
    {"file": "game/em_player.c", "field": "spad3B8D",
     "pattern": r"live\.scene\.spad3B8D\s*=\s*s->spad3B8D;",
     "reason": "live_scene_load: the player stage's per-stage view of canonical 3B8D (0015BA50 / "
               "0015B130 read it; the stage never writes 3B8D, only 3B8F is stored back)",
     "removed_by": "permanent (a per-stage view)"},
]

# The migrated progress/request bytes (HK): the files whose code names the
# address, and the original function each one stands for.
REACHERS = {
    0x00810707: {
        "game/em_player_frame.c": "0015CF90 (em_player_0015BCF0): D_00810707 = +0x234 every player stage",
        "game/em_scene_bindings.c": "001B07C0 (w_001B07C0): reads it into +0x234",
        "game/em_player_stage_live.c": "0021C270's store (=1): the stage workers' globals pointer, loaded "
                                       "before every player stage (w_load, L01)",
    },
    0x0081083C: {
        "game/em_player_stage_live.c": "0021C440's read (the +5 = 0xB reaction): the stage workers' "
                                       "per-stage view, loaded before every player stage (w_load, L01)",
    },
    0x00810813: {
        "game/em_director.c": "008253F0's beat step, legacy stand-in until WP-10: its state-1 dispatch "
                              "reads it, the beat completions store 0x10/0x20/0xFF",
    },
    0x00810CC3: {
        "game/em_pickup.c": "001B6EA0's key take (em_pickup_owner_take adds to D_00810CC3[t]) and the "
                            "key accessor em_pickup_keys",
        "game/em_opening_runtime.c": "00823E80's 001C4760(0, 1) (0x823F84) and its completion report",
        "game/em_director.c": "008253F0 beat 0's 001C4760(1, 1) (0x8255CC..0x8255D4), legacy stand-in "
                              "until WP-10",
        "game/em_opening_control_test.c": "reads D_00810CC3[0] to check the opening hand-off "
                                          "(test instrumentation, never written)",
        "game/em_director_original.c": "001C4760 (em_director_original_001C4760, byte-matched "
                                       "src/func_001C4760.c) bound over the canonical storage "
                                       "(em_director_original_001C4760_scene)",
        "game/em_director_original.h": "declares that binding",
    },
    0x00810CB6: {
        "game/em_player.c": "0015BA50's busy test: the stage scene's pointer (live_scene_load)",
    },
    0x008106F1: {
        "game/em_player.c": "0015BA50's busy test and 0021C270's store (the stage workers, bound "
                            "since L01): the stage scene's pointer (live_scene_load)",
    },
    0x00810792: {},   # no port reacher yet: em_truck_original's pointer is bound by WP-12
    0x00810793: {},   # no port reacher yet: em_director_original / em_roger are bound by WP-10 / WP-9
}
REACH_CALL = {0x00810CC3: re.compile(r"\bem_director_original_001C4760_scene\s*\(")}

# Declarations whose comment or name names a canonical byte outside em_scene_state.h.
ALLOWED = [
    {"file": "game/em_frame_trace.h", "name": "selector",
     "reason": "trace record of the canonical 3B8D value sampled at the 0x1AE040 entry "
               "(instrumentation, never read back)",
     "removed_by": "permanent"},
    {"file": "game/em_script.h", "name": "skip_request",
     "reason": "the interpreter's per-tick view of canonical 3B91 (relabelled in S11b): "
               "em_opening_runtime publishes the canonical byte before each tick and writes "
               "both; since WP-4 the AREA11 interaction host loads it before each owner tick "
               "and stores it after (script_load/script_store)",
     "removed_by": "permanent (a per-tick view)"},
    {"file": "game/em_interaction_frame.h", "name": "selector",
     "reason": "per-call view of canonical 3B8D (relabelled in WP-4): the AREA11 interaction host "
               "loads the whole EmInteractionFrame from the canonical storage before every entry "
               "point and stores it after (view_load/view_store); no value survives between calls",
     "removed_by": "permanent (a per-call view)"},
    {"file": "game/em_interaction_frame.h", "name": "ready",
     "reason": "per-call view of canonical 3B92, as `selector` above (relabelled in WP-4)",
     "removed_by": "permanent (a per-call view)"},
    {"file": "game/em_interaction_scan.h", "name": "selector",
     "reason": "the 00184BA0 gate's per-call input: the AREA11 interaction host's Use scan (live "
               "since WP-4) fills it from canonical 3B8D before every scan and never stores it",
     "removed_by": "permanent (a per-call input)"},
    {"file": "game/em_player_floor.h", "name": "spad3B8D",
     "reason": "EmPlayerStageScene: the player stage's per-stage view of canonical 3B8D, loaded by "
               "em_player.c live_scene_load before 0015BA50; the stage only reads it",
     "removed_by": "permanent (a per-stage view)"},
    {"file": "game/em_spawn_table.h", "name": "spad3B8D",
     "reason": "EmSpawnIo: 001B07C0's per-call input, filled from canonical 3B8D by w_001B07C0 "
               "before every placement and never stored back",
     "removed_by": "permanent (a per-call input)"},
    {"file": "game/em_spawn_table.h", "name": "d810707",
     "reason": "EmSpawnIo: 001B07C0's per-call input, filled from the canonical progress byte "
               "D_00810707 by w_001B07C0 before every placement and never stored back",
     "removed_by": "permanent (a per-call input)"},
    {"file": "game/em_camera_area11_specials.h", "name": "s3B8D",
     "reason": "EmCamSpecialsScratch: 00193EB0's per-call input (its area gate reads 3B8D); the "
               "translation is unbound",
     "removed_by": "permanent (a per-call input); L15 fills it from em_scene_state() when it binds 00193EB0"},
    {"file": "game/em_coll_list_passes.h", "name": "s3B8D",
     "reason": "EmCollListGlobals: the list passes' per-call view of 3B8D, read only by the 001A8BE0 / "
               "001A9F60 gates; em_collision_world_close_out_001AAD00 fills it from em_scene_state() "
               "before every 001AAD00 (census L08) and never stores it back",
     "removed_by": "permanent (a per-call input)"},
    {"file": "game/em_player_hang.h", "name": "scripted",
     "reason": "EmPlayerHangScene: 001647D0's per-call input, read once at entry through its scene "
               "worker; no worker it calls writes 3B8D (001647D0 is not on the census route)",
     "removed_by": "permanent (a per-call input)"},
    {"file": "game/em_player_reaction.h", "name": "scripted",
     "reason": "EmPlayerReactionScene: the reaction lane's per-call input of 3B8D, refreshed by its "
               "`refresh` worker; the translation is unbound",
     "removed_by": "permanent (a per-call input); the FLOOR closure's binder (L02) fills it from "
                   "em_scene_state() when it binds the +4 = 2 reaction states"},
    {"file": "game/em_player_reaction.h", "name": "d81083C",
     "reason": "EmPlayerReactionScene: 0021F330's per-call input of D_0081083C, refreshed by its "
               "`refresh` worker; the translation is unbound",
     "removed_by": "permanent (a per-call input); the FLOOR closure's binder (L02) fills it from the "
                   "canonical progress byte when it binds the +4 = 2 reaction states"},
    {"file": "game/em_player_stage_workers.h", "name": "d81083C",
     "reason": "EmPlayerStageGlobals: 0021C440's per-stage view of D_0081083C, loaded from the "
               "canonical progress byte before every player stage (em_player_stage_live.c w_load); "
               "the stage never writes it",
     "removed_by": "permanent (a per-stage view)"},
    {"file": "game/em_player_recovery.h", "name": "spad3B8D",
     "reason": "EmPlayerRecoveryScene: 001751A0's per-call input, filled by the `scene` worker at "
               "each routine entry; the translation is unbound",
     "removed_by": "permanent (a per-call input); L11 fills it from em_scene_state() when it binds 001751A0"},
    {"file": "game/em_player_running_jump.h", "name": "spad3B8D",
     "reason": "EmPlayerRunningJumpScene: 001AA4E0's per-call input (0015EC50 reads the list only "
               "while 3B8D is clear); the translation is unbound",
     "removed_by": "permanent (a per-call input); L11 fills it from em_scene_state() when it binds 0015EC50"},
    {"file": "game/em_player_slide.h", "name": "scripted",
     "reason": "EmPlayerSlideScene: the slide routines' per-call input of 3B8D; the translation is "
               "unbound",
     "removed_by": "permanent (a per-call input); L03 fills it from em_scene_state() when it binds 0016C6A0"},
    {"file": "game/em_roger.h", "name": "alternate",
     "reason": "EmRogerStory: the unbound Roger translation's value view of D_00810793 (event 0x3B, "
               "read by 008237E0's initial branch)",
     "removed_by": "WP-9 (L22): Roger's binding points the story at the canonical progress bytes"},
    {"file": "game/em_roger.h", "name": "auxiliary",
     "reason": "EmRogerStory: the unbound Roger translation's value view of D_00810813, which "
               "008237E0 also writes (0x11 at 0x823A04)",
     "removed_by": "WP-9 (L22): Roger's binding points the story at the canonical progress bytes"},
    {"file": "game/em_director_original.c", "name": "completion",
     "reason": "a beat-table constant: the value 008253F0's completion stores into D_00810813, not "
               "storage of the byte",
     "removed_by": "permanent (a constant)"},
    {"file": "game/em_director_original.c", "name": "step_store",
     "reason": "a beat-table constant: the original address of the completion's store to D_00810813, "
               "not storage of the byte",
     "removed_by": "permanent (a constant)"},
    {"file": "game/em_game_internal.h", "name": "next_step",
     "reason": "CineBeat: the legacy director's beat-table constant (the D_00810813 value a "
               "completion stores), not storage of the byte",
     "removed_by": "WP-10 (L21): deleted with em_director.c's kCineBeats when 008253F0 is bound"},
]

BYTE_TOKEN = re.compile(r"3B8D|3B91|3B92|810707|810792|810793|810813|810CC3|810CB6|8106F1|81083C",
                        re.IGNORECASE)
RETIRED = ("frame_selector", "cine_step", "opening_key_item_zero")
DECL = re.compile(
    r"^\s*(?:static\s+)?(?:const\s+)?(?:volatile\s+)?"
    r"(?:u?int(?:8|16|32|64)_t|unsigned(?:\s+(?:char|short|int))?|char|short|int|bool)\s+"
    r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?:,\s*[A-Za-z_]\w*\s*)*;\s*(?:/[*/](.*))?$")
ASSIGN = r"\b{}\s*(?:=(?!=)|\+\+|--|[-+|&^]=)"
FILE_SCOPE_STATE = re.compile(r"^(?:static\s+)?EmSceneState\s+\w+\s*(?:=|;)")


def sources(root):
    return sorted(p for p in root.rglob("*") if p.suffix in (".c", ".h", ".m"))


def code_only(text):
    """The text with comments and string literals blanked (line numbers kept)."""
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r'"(?:\\.|[^"\\])*"', '""', text)


def main():
    failures = []
    files = sources(SRC)

    # 1. The retired copies.
    retired = re.compile(r"\b(" + "|".join(RETIRED) + r")\b")
    for p in files + sources(ROOT / "tests"):
        for n, line in enumerate(code_only(p.read_text(errors="replace")).splitlines(), 1):
            m = retired.search(line)
            if m:
                failures.append(f"{p.relative_to(ROOT)}:{n}: {m.group(1)} (a retired copy)")

    # 2. Writers.
    views_used = set()
    for field, allowed in WRITERS.items():
        pattern = re.compile(ASSIGN.format(re.escape(field)))
        for p in files:
            rel = p.relative_to(SRC).as_posix()
            for n, line in enumerate(p.read_text(errors="replace").splitlines(), 1):
                code = line.split("//")[0]
                if not pattern.search(code) or rel in allowed:
                    continue
                view = [i for i, v in enumerate(VIEW_LOADS)
                        if v["file"] == rel and v["field"] == field and re.search(v["pattern"], code)]
                if view:
                    views_used.update(view)
                    continue
                failures.append(f"src/{rel}:{n}: writes {field}; its writers are "
                                f"{', '.join(sorted(allowed))}")
    for i, v in enumerate(VIEW_LOADS):
        if i not in views_used:
            failures.append(f"stale VIEW_LOADS entry {v['file']}:{v['field']} (remove it)")

    # 3. One owner of the state.
    for p in files:
        rel = p.relative_to(SRC).as_posix()
        if rel == "game/em_scene_bindings.c":
            continue
        for n, line in enumerate(p.read_text(errors="replace").splitlines(), 1):
            if FILE_SCOPE_STATE.match(line):
                failures.append(f"src/{rel}:{n}: file-scope EmSceneState outside the bindings")

    # 4. Annotated shadow declarations (the comment or the name names the byte).
    used = set()
    for p in files:
        rel = p.relative_to(SRC).as_posix()
        if rel == "game/em_scene_state.h":
            continue
        for n, line in enumerate(p.read_text(errors="replace").splitlines(), 1):
            m = DECL.match(line)
            if not m:
                continue
            token = BYTE_TOKEN.search(m.group(2) or "") or BYTE_TOKEN.search(m.group(1))
            if not token:
                continue
            hit = [i for i, a in enumerate(ALLOWED) if a["file"] == rel and a["name"] == m.group(1)]
            if hit:
                used.update(hit)
            else:
                failures.append(f"src/{rel}:{n}: `{m.group(1)}` is declared as storage of "
                                f"{token.group(0)}; use em_scene_state()")
    for i, a in enumerate(ALLOWED):
        if i not in used:
            failures.append(f"stale ALLOWED entry {a['file']}:{a['name']} (remove it)")

    # 5. Who reaches the migrated progress/request bytes.
    reached = set()
    for p in files:
        rel = p.relative_to(SRC).as_posix()
        if rel == "game/em_scene_state.h":
            continue
        code = code_only(p.read_text(errors="replace")).splitlines()
        for address, allowed in REACHERS.items():
            literal = re.compile(r"\b0x0*%X[uU]?\b" % address, re.IGNORECASE)
            call = REACH_CALL.get(address)
            for n, line in enumerate(code, 1):
                if not literal.search(line) and not (call and call.search(line)):
                    continue
                if rel in allowed:
                    reached.add((address, rel))
                else:
                    failures.append(f"src/{rel}:{n}: reaches D_{address:08X}; its reachers are "
                                    f"{', '.join(sorted(allowed)) or 'none yet'}")
    for address, allowed in REACHERS.items():
        for rel in allowed:
            if (address, rel) not in reached:
                failures.append(f"stale REACHERS entry D_{address:08X} {rel} (remove it)")

    if failures:
        print("scene no-shadow (G6): FAIL")
        for f in failures:
            print("  " + f)
        return 1
    print(f"scene no-shadow (G6): PASS ({len(files)} files; {len(WRITERS)} canonical fields "
          f"with original writer sets; {len(REACHERS)} migrated progress/request bytes with "
          f"their reachers; {len(VIEW_LOADS)} view loads; {len(ALLOWED)} listed "
          f"non-canonical declarations)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
