# AREA01 placement [38]: talk owner 0x825740

2026-10-03. Standalone translation added to the existing
`em_area01_overlay.c` / `.h`; this does not bind another live owner or remove
the AREA01 fail-stop gate.

The original placement table at 0x82BD50 uses 0x28-byte records. Record [38]
is at 0x82C340, and its callback at +0x24 is 0x825740, checked directly in
`../Extermination/build/s87/route/15_level_exit/eeMemory.bin`. The matched
decomp source is `func_overlay_AREA01_00825700.c` (link address is runtime
minus 0x40), 464 bytes. The existing overview records the same owner and
placement in `AREA01_OVERVIEW.md` sections 7.1 and 8.

This owner is absent from the kept first-visit end pools. Its initial
execution is not established by that absence: the original setup runs its
model and animation workers before testing D_0081075A. When that byte is
zero, it sets state 3; otherwise it asks 001BA1C0(self, 6), keeping state 1
only when that result is zero. States 2 and 3 both call 001BA540 and
001AFC10. This is why the callback needs a translation even when it is not a
persistent first-visit NPC.

In state 1, step 0 starts script 0x82A7B0 when Use bit 4 is set. Step 1 ticks
the script; a nonzero result clears the step and Use byte and selects clip
1 with blend 30. The animation, visibility and actor draw callback run
after either step. Setup copies the current yaw into the talk block and
D_0028A5C4 into the owner; all original loads, stores and calls retain their
order. The translation uses the existing hooks and fault record.

Verification, native arm64 macOS, from the port root:

```sh
python3 tools/test_area01_overlay_reference.py
EM_TEST_FULL=1 python3 tools/test_area01_overlay_reference.py
```

Both passed on 2026-10-03. The original instructions are read from the
user's captured AREA01 RAM after validating its overlay text against the
extracted overlay and its boot text against the pinned ELF. Placement [38]
uses designed cases over the captured control-room NPC record, explicitly
calling 0x825740; these are not represented as captured executions of [38].

- Quick: 184 cases for this owner; 2,192 cases for the complete overlay
  suite, 3,524 executions including poisoned initial bytes.
- Full: 1,131 cases for this owner, including all 256 values of state, step,
  Use and D_0081075A; 14,034 cases for the complete suite and 21,199
  executions including poisoned initial bytes.
- Both execute all 79 reachable non-branch instruction words of 0x825740
  and all 2,072 such words across the 15 entries. Callee arguments, memory
  at every call entry, ordered memory accesses and final memory agree. The
  full run compares all 32 MiB and the scratchpad at every call entry.
- Setup gates and script completion cover zero, positive and negative
  return bit patterns. Scribbling hooks check the re-read of +0x0D after
  calls and setup's yaw, global word and story byte. The shared fail-stop
  checks cover the new entry alongside the others: latched fault, missing
  hook table or fault pointer, and refused memory/call operations.
- The suite's hook-contract pass completed 641 native runs on 26 cases,
  covering 53 hooks, the actor callback and all 15 entry points.

Receipts are ignored local files:
`build/area01-smoke-scaffold/overlay-quick.log` and
`build/area01-smoke-scaffold/overlay-full.log`. This evidence proves the
standalone owner on the tested cases. It does not certify its future live
worker bindings, the conversation's presentation or a route containing it.
