# Render, area-load and math leftovers (census lanes L31, L37, L29b, L20)

Date: 2026-09-23. Lane "render-verify-rest". Scope: the rows of
`docs/FIRST_LEVEL_CENSUS.md` that were still missing or unverified in
**L31-background-weather-load** (5 missing, 5 unverified),
**L37-sdk-math-leaves** (3 missing), **L29b-shadow-gs** (5 unverified) and the
one missing row of **L20-message-service**. **L22-roger-encounter** has no
missing or unverified row; its 24 verified-unbound rows get binding notes
only (section 5).

Files (new, not yet in the build):

| File | Contents |
|---|---|
| `src/game/em_render_verify_rest.h/.c` | 001C1DC0, 001C1E70, 001C1E80, 001C1E90, 001C1F50, 001E2260, 001E2270, 001E2280, 001E0CF0, 001C22A0, 001C2360, 001027E0, 00102850, 001000E0, 001FCF10, 001D4B50, 001DA1E0, 001DA290 |
| `tools/test_render_verify_rest_reference.py` | the original-instruction oracle for all of the above, and the per-function verification of em_shadow_original.c's 001DA080, 001DA310 and 001D5C80 |

No original code, data or disassembly is in these files. The data the
routines read (the D_00250F30 colour, the D_002531D0 template, the table
words, the render-context bytes) comes from the user's ELF and captured RAM
at run time; the translations take it as arguments.

## 1. Status per function

"Before" is the census row; "after" is what this lane delivers. Nothing here
is bound into the live game (section 4), so no row becomes live.

| Address | Decomp | Before | After | What it does |
|---|---|---|---|---|
| 001C1DC0 | BM | unverified (em_scene_bindings.c `w_001C1DC0`: only the 001C1EA0 weather spawn, the rest reported no-effect) | verified-unbound | Area render init: 001D2830 (0,1) (1,1) (2,0) (0x24,0) (0x20,0) (0x21,0) (0x22,0) (0x25,0), then 001C1E70, 001C1E80, 001C1E90, 001C1EA0(D_008101D0), 001C1F50. |
| 001C1E70 | BM | missing | verified-unbound | A jump to 001D52E0 (the level cell grid publish; takes no argument). |
| 001C1E80 | BM | missing | verified-unbound | A jump to 001D8FD0 (the area fog; takes no argument). |
| 001C1E90 | BM | missing | verified-unbound | Returns. No reads, no writes. |
| 001C1F50 | NM (.s) | unverified (em_background_gs.h comment only) | verified-unbound | Key = (D_00810700 << 8) + D_00810701, re-read before each of four chains. Flags: key 0x1500 -> 001D2830 (0x20..0x23, 1); keys 0x0B00, 0x0C00, 0x0D00, 0x0E00, 0x0F00, 0x0F01, 0x1100, 0x1200 -> (0x20,1) (0x21,1) (0x22,0); any other key -> (0x20,0). TEX0: eight keys (all of those but 0x0C00) pass their 64-bit tag to 001E2260 (AREA11, key 0x0B00: 0x20069F0121323200). Always 001E2270(&D_00250F30). Key 0x1500 also 001E2280(0x20042B05DD321D00). Keys 0x1200 and 0x0F00 also 001D2830(0x25, 1). |
| 001E2260 | BM | unverified (em_background_gs.h comment; em_frame_render_heads worker) | verified-unbound | ctx+0x1D0 = the 64-bit argument. |
| 001E2270 | BM | unverified (em_background_gs.h comment) | verified-unbound | ctx+0x1C0..0x1CF = the 16 bytes at the argument (one quadword copy). |
| 001E2280 | BM | not in the census (never ran; only area 0x1500 reaches it) | verified-unbound | ctx+0x1E0 = the 64-bit argument. Translated because 001C1F50 can reach it. |
| 001E0CF0 | BM | unverified (em_frame_render_heads worker slot) | verified-unbound | 001E0CC0(); if 001D2910(0x20): if 001D2910(0x21), ctx+0x1D8 = 001E1E60(ctx+0x180, 3); if 001D2910(0x22), ctx+0x1E8 = 001E1AD0(ctx+0x1E0, 3). |
| 001C22A0 | BM | missing | verified-unbound | Library-entity model bind through table D_0028A59C: model = 001C6120(D_0028A59C[0], +0xD); 001CA5E0(self, model, 2); +0xC = low byte of 001C6150(+0x44 word); if the signed half D_00275BCC < +0xC, return 1; else +0x110 + 4i = 001AF780() for i < +0xC (re-read each pass), +9 = +0xC, 001CB5B0(+0xC), 001C62C0(self), return 0. |
| 001C2360 | BM | missing | verified-unbound | The same through D_0028A56C. |
| 001027E0 | AW (.s) | missing | verified-unbound | VU0 rigid inverse of a row-vector 4x4: rows 0..2 of the result = the transposed 3x3 (w lanes 0, from the cleared translation copy); row 3 = (0 - (t.x row0' + t.y row1' + t.z row2'), in[15]), with the VU0 ACC chain (MULAx, MADDAy, MADDz, then a SUB). All loads precede the stores, so out may alias in. |
| 00102850 | AI (.s) | missing | verified-unbound | Q = 1.0 / s (VDIV, reciprocal form (3,0)), out = v x Q on all four lanes. out may alias v (00209280 calls it in place with s = 12.0). |
| 001000E0 | BM | missing | verified-unbound | Returns 1 when 001274B0(a, b) <= 0, else 0, a and b the full 64-bit argument registers (the soft-float double compare; em_sdk_soft_float_001274B0). |
| 001FCF10 | BM | missing | verified-unbound | 001FCB90(0x10E, 0xCC, 5, 0) (the BATTERY page's message-bank group-5 line, called from 002149F0 state 4). |
| 001D4B50 | BM | unverified (em_gfx comment) | verified-unbound | 001D49D0(obj) then 001D4B10(obj): the class-2 receiver's clip-pass packets. |
| 001DA1E0 | BM | unverified | verified-unbound | The 0x80-byte CNT/DIRECT record at channel `a0`'s cursor (ctx+0x10+4*a0): +0 half 7, +3 byte 0x10, +4 word 0 (bytes +2 and +8..+0xF keep their value), +0x10..+0x1B 0, +0x1C 0x50000006, +0x20 0x5022400000008001, +0x28 0x44441, +0x30..+0x3B 0, +0x3C a2, +0x40..+0x7F the 64 bytes at a1 (read one quadword at a time after the header stores); the cursor += 0x80; returns record+0x10. |
| 001DA290 | CL (.s) | verified-unbound (as a worker call in em_shadow_original) | packet content added here | 001D1F80(**a0**, 2, 9), then 001DA1E0(a0, a stack copy of D_002531D0, a1). See Findings 2. |
| 001DA080 | BM | unverified (inline in em_shadow_original.c) | verified-unbound (tree file unchanged) | The two node picks: the smallest 00102738 dot with *a3 (near, D_00817FB0) and the largest with D_00817FC0 (far, D_00817FA0), both starting at node 1. |
| 001DA310 | NM | unverified (em_shadow_original.c `box_pass`) | verified-unbound (tree file unchanged) | One destination-alpha box pass; see docs/SHADOW_ORIGINAL.md. |
| 001D5C80 | NM | unverified (em_shadow_original.c `receivers`) | **fails** with the tree file; verified-unbound once the two hunks of Findings 1 are applied | The receiver sweep; see docs/SHADOW_ORIGINAL.md. |

Decomp codes as in the census (BM byte-matched, NM NEARMISS, AW asm words,
AI inline asm, CL C linked from asm). For NM, AW, AI and CL units the
original instructions (the split listing) were read, not the decomp C.

## 2. Findings

1. **em_shadow_original.c: 001D2D20 and 001D5C80's cell math use the wrong
   COP1 add/sub.** Both run as EE COP1 code (001D2D20: one add.s, one sub.s;
   001D5C80: six add.s, ten sub.s, six div.s). The EE pre-trims the smaller
   operand of add.s/sub.s (docs/EE_FLOAT_MODEL.md, ADD.S row). The module's
   `add`/`sub` are the plain truncated sums, which the model shows wrong on
   about a quarter of random operand pairs. Effects:
   - `projection()`: far - near = 16711680 - 0.1 is 16711680 on the EE
     (the 0.1 is trimmed away) and 16711679 in the module. So the z columns
     of both clip matrices (D_70003400 / D_70003440) differ by one ulp in
     every capture and route beat.
   - `receivers()`: the grid row/column range can differ. The test's
     `boundary-row` case pins the anchor and puts the highest node at
     z = 0x412E8F89 (a searched input, not data). There the original scans
     rows 0..0 and the module scans rows 0..1, and it probes object id 129
     that the original never reaches.

   The fix is two hunks in `src/game/em_shadow_original.c`. This lane does
   not own the file, so the lead applies them:
   ```
   +#include "game/em_ee_float.h"            (after the em_shadow_original.h include)
   projection():  sum = em_ee_add(far, near);   diff = em_ee_sub(far, near);
   receivers():   every add/sub/divide in the six cx/cz/x0/x1/z0/z1 lines ->
                  em_ee_add / em_ee_sub / em_ee_div
   ```
   A scratch copy with exactly these hunks was checked against:
   - this test: quick and EM_TEST_FULL=1, all 7 captures, 15 route beats,
     48 synthetic cases and the boundary case (68 chains);
   - `tools/test_shadow_original_reference.py`: PASS;
   - `tests/shadow_original_test.c` (ASan/UBSan): PASS.

   `EM_RVR_SHADOW_SRC=<path>` points this test at such a copy. The module
   is not in COMMON, so the fix changes no live behaviour.
2. **001DA290 passes its own a0 to 001D1F80.** The listing sets only a1 = 2
   and a2 = 9 before the call. The decomp C (asm-linked, not byte-matched)
   writes `func_001D1F80(0, 2, 9)`, which is not what executes. The unit
   oracle caught this with a0 = 1. 001DA6A0 passes 0, so on the shadow route
   both forms give 0; em_rvr_001DA290 passes a0. The decomp comment should
   be corrected (decomp repo, not this lane).
3. **em_player_fall.c: the 001000E0 argument is truncated.** 0017C580 copies
   00128350's whole 64-bit double into a0 (a 128-bit register copy) and
   calls 001000E0(a0, 0). EmPlayerLandWorkers types both `convert_00128350`'s
   result and `test_001000E0`'s arguments as `int`, so only the low word
   survives. Executed original: 001000E0(double 1.0, 0) = 0, but
   001000E0(low word of 1.0 only, 0) = 1. The low word of a float's double
   holds only the float's three lowest mantissa bits, so with the int path
   the answer no longer depends on the sign or size of player+0x220: every
   positive value whose three lowest mantissa bits are 0 (e.g. 1.0) takes
   the `reset_to_reaction` branch when +0x234 == 1, which the original does
   not. `test_player_fall_reference.py`
   scripts both hooks identically on both sides, so it cannot see this. Fix
   (lead / fall lane): make the workers `uint64_t` (em_sdk_soft_float_w_00128350
   already returns the 64-bit double), and bind test_001000E0 to
   em_rvr_001000E0.
4. **Census rows that were already exercised.** 001DA080, 001DA310,
   001D5C80, 001D4B50 and 001DA1E0 run unstubbed inside
   `test_shadow_original_reference.py`'s 001DA6A0 execution. That test never
   names them, so the textual classification marked them unverified. This
   test now intercepts each one and checks its inputs and outputs
   separately.

## 3. Verification

`python3 tools/test_render_verify_rest_reference.py`: the default run takes
about 1.5 s (4 CPU s). `EM_TEST_FULL=1` takes about 30 s.

The interpreter is `test_player_fall_reference.FallEE`, the shared EE core
with every COP1 and VU0 macro result taken from `tools/ee_float_model.py`.
It adds PEXTLW/PEXTUW, VCLIPW.xyz and the CLIP register, each checked on
synthetic words before any original code runs. Hooked callees are recorded
and scripted identically for both sides. For every routine the test asserts
that the hooked set plus the executed set is exactly its set of call and
jump targets.

| Section | What runs | Quick | Full |
|---|---|---|---|
| A 001C1F50 | every area key; hooked 001D2830; executed 001E2260/001E2270/001E2280; call log and ctx+0x1C0..0x1EF compared | 304 keys (the nine special keys, their +-1 / +-0x100 neighbours, 0, 0xFFFF, a sample) | 65,536 keys |
| A 001C1DC0 | keys as above; hooked 001D2830, 001D52E0, 001D8FD0, 001C1EA0; the thunks and 001C1F50 executed (each entered once) | 40 | 65,536 |
| A stores | 001E2260/001E2270/001E2280 with random values; the whole 0x200-byte context compared | 200 | 2,000 |
| A 001E0CF0 | all 8 flag combinations with varied nonzero values; hooked 001E0CC0, 001D2910, 001E1E60, 001E1AD0 | 32 | 320 |
| A model binds | 001C22A0 and 001C2360 x cap (-0x8000..0x7FFF) x count word (0..0xFFFFFF05, low byte stored); 0x520-byte random records | 60 | 180 |
| A route | 001C1F50 and 001E0CF0 over captures and beats with their own key; 001D2910 executed and its results replayed to the native side | 4 | 22 (7 captures + 15 beats) |
| B 001027E0 | 5 captured view/projection/clip matrices + random/special bit patterns, every fifth in place | 305 | 3,005 |
| B 00102850 | random/special v and s (zero, denormal, Inf, NaN, 12.0), half in place | 600 | 6,000 |
| B 001000E0 | special doubles, doubles of floats, random 64-bit patterns | 410 | 4,010 |
| C 001FCF10 | the call | 1 | 1 |
| D units | 001D4B50 (hooked 001D49D0/001D4B10); 001DA1E0 (4 channels, garbage windows, payload in place at +0x40 and overlapping the header); 001DA290 (hooked 001D1F80) | 190 | 1,900 |
| D shadow | 001CB590 + 001DA6A0 executed with 001DA080, 001DA290, 001DA1E0, 001DA310, 001D5C80, 001D4B50 intercepted, against em_shadow_original_001DA6A0 (see below) | playable, opening, 06, 14, 8 synthetic, boundary | 7 captures, 15 beats, 48 synthetic, boundary |
| E fail-stop | NULL workers, a failing worker, the latch, short views, the bone-table bound | 7 | 7 |

The D shadow section checks these fields against the native plan:
- 001DA080: its arguments (D_00817FB0, D_00817FA0, the player),
  D_00817FC0 and *a3 at entry, both picks, and both indices.
- 001DA290/001DA1E0: 001D1F80(a0, 2, 9); the arguments (the D_002531D0
  template, a1); em_rvr_001DA1E0 over the pre-call window gives the same
  record, cursors and return value.
- 001DA310 x2: anchor, size, colour, model object, colour row, camera and
  normal uploads, the 001D7080 RGBAQ, D_70003AC0 at its 001D4FB0 (the clip
  pass), and D_70003AC0 restored.
- 001D5C80: its argument, both clip matrices, the 001C6120 probe order (ids),
  and the 001D4FB0 draw order with each 001D4B50 class-2 follow-up.
- 001D4B50: em_rvr_001D4B50's worker calls.

Synthetic cases jitter the nodes, move the whole body by up to 40 units,
leave two nodes, and vary D_00817FF0. Both outcomes of every conditional
branch of 001DA080 are asserted reached.

Capture corroboration: in all 22 captures and beats the render context
already holds what 001C1F50 writes for AREA11 (ctx+0x1D0 =
0x20069F0121323200, ctx+0x1C0 = the D_00250F30 colour), and 001D2910
answers 1, 2, 0 for flags 0x20, 0x21, 0x22.

Result with the tree as it is: sections A, B, C, D units and E pass.
D shadow fails at the first 001D5C80 check (Findings 1). With the two hunks
applied (via EM_RVR_SHADOW_SRC), both modes pass.

## 4. Binding notes (for the coordinator)

Nothing here is called by the live game yet. Makefile hunks:

```
.PHONY: test-render-verify-rest-reference
test-render-verify-rest-reference:
	python3 tools/test_render_verify_rest_reference.py
```

When a routine is bound, add to COMMON: `src/game/em_render_verify_rest.c`
and `src/game/em_sdk_soft_float.c` (001000E0 calls
em_sdk_soft_float_001274B0; that module is not in COMMON yet). The lane
build with both files added links with zero warnings.

- **001C1DC0**: replaces `w_001C1DC0` in `em_scene_bindings.c` (called from
  the 0x1AE040 frame machine, `em_scene_frame.c` SF_001AE040), which today
  runs only the weather spawn and reports UM_001C1DC0. Workers:
  - w_001D2830: missing, L32. Every call must fault until it is translated,
    so the binding stays unwired until then.
  - w_001D52E0: missing, L30.
  - w_001D8FD0: em_fog_gs, verified-unbound. Its stand-in is the exported
    fog record in the scene manifest.
  - w_001C1EA0: `em_area11_spawn_weather_001C1EA0`, live.
  - w_001E2260 / w_001E2270 / w_001E2280: em_rvr_001E2260 / 001E2270 /
    001E2280 over the render context's bytes. 001E2270's source is the 16
    bytes of D_00250F30 (boot ELF data; the port needs them from the
    user's ELF, as an asset).
  - The key: D_00810700/701 of the scene state.
- **001C1F50**: reached only through 001C1DC0.
- **001E0CF0 / 001E2260**: the `w_001E0CF0` / `w_001E2260` slots of
  `em_frame_render_heads.h` (001C1D00's callees, census L32; its live
  stand-in is `em_render_frame.c em_render_001C1D00`). Workers:
  - w_001E0CC0: missing, L30.
  - w_001D2910: boundary, the render-flag read.
  - w_001E1E60: em_background_gs.h / Metal, verified-unbound.
  - w_001E1AD0: never ran in the census; flag 0x22 stays clear in every
    capture (`001D2910` results 1, 2, 0), so AREA11 never calls it. It must
    still be bound, or fault.
- **001C22A0 / 001C2360**: the model binds that 001C5760 / 001C5680 call
  (census L17; the live per-node children, em_indicator_child, take the bind
  as always successful: the port keeps no bone slots for them). Workers:
  - 001C6120: em_pose_host_workers / em_owner_services_original.
  - 001CA5E0: em_roger_actor_original.
  - 001C6150: em_status_models / em_roger_actor_original.
  - 001AF780: em_owner_services_original.
  - 001CB5B0: anim_bone_array_setup, L33.
  - 001C62C0: bone_init_default_1.
  - The table words D_0028A59C[0] / D_0028A56C[0] and D_00275BCC come from
    the loaded model library state.
- **001027E0**: the inverse step of 00102CD0 (the look-at in the camera
  commit). Bound since census L13..L16: em_cs_00102CD0
  (em_census_standins) calls em_rvr_001027E0; em_mat4_lookat_gs is deleted
  (docs/CAMERA_LIVE.md).
- **00102850**: 00209280 (the BATTERY block, live stand-in `em_hud.c`)
  calls it in place with 12.0 at 0x700038C0.
- **001000E0**: `EmPlayerLandWorkers.test_001000E0` of em_player_fall
  (verified-unbound). See Findings 3: the worker must take uint64_t.
- **001FCF10**: 002149F0 state 4 (the BATTERY page; live stand-in
  `em_battery_ui.c` / `em_area11_interaction_host.c battery_finished`).
  Worker 001FCB90 is the L20 stand-in row (`em_hud.c` legacy message
  lookup). Its original body indexes the message bank D_0028A498 by group
  a2 and calls 001FE070 (em_message_draw_original, verified-unbound).
- **001D4B50**: em_shadow_original calls it through `w_receiver` for class-2
  receivers (the GS-side worker, Metal `em_gfx_shadow_*`). Its workers
  001D49D0 / 001D4B10 are census boundary (the clip-pass VIF packets; the
  port's clip kernel translation is `em_vu1_shadow_clip.h`).
- **001DA1E0 / 001DA290**: the packet content behind em_shadow_original's
  `w_alpha_clear` (Metal `em_gfx_shadow_alpha_clear` draws the strip
  natively). Use em_rvr_001DA290 wherever the port compares or replays the
  original packet bytes. Needs D_002531D0 (64 bytes of ELF data) and a
  render-context/cursor view.

## 5. Verified-unbound rows of these lanes (binding notes only)

- **L31**:
  - 001E1E60 and 001D2300: `em_background_gs.h` + the Metal backend
    (`test_background_reference.py`); not in COMMON. 001E1E60 is
    em_rvr_001E0CF0's w_001E1E60.
  - 001D8FD0, 0021B970, 0021BA80: `em_fog_gs.h`
    (`test_area11_fog_reference.py`). The live stand-in is the exported
    fog record.
  - 0021B920 and 0021B9A0: `em_packet_chain_original`
    (`test_packet_chain_reference.py`, docs/PACKET_CHAIN.md). 0021B920 is
    live: `em_fog_gs_coefficients` calls it for the Metal fog.
- **L29b**:
  - 001D98A0, 001D9EE0, 001DA290, 001DA6A0, 001D4CD0, 001D4FB0:
    `em_shadow_original.c` (not in COMMON) and its GS side (`em_gfx.h`
    em_gfx_shadow_*, `em_shadow_gs.h`), all checked by
    `test_shadow_original_reference.py`.
  - The live route into them is 0015C160 (census L29,
    `em_shadow_original_route_0015C160`).
  - Apply Findings 1 before binding.
- **L37**:
  - 0011C4C8, 0011D878, 0011E398, 0011DBB8, 0011DE90: `em_sdk_math_original`
    (in COMMON; the stand-ins are em_player.c `probe_atan` with host atanf,
    and em_player_heading.c with host trig).
  - 0011DB90, 0011FD78, 00126AB8, 00126BE8, 00127398, 001274B0, 00127728,
    00127758, 001277B0, 00128320: `em_sdk_soft_float` (not in COMMON; the
    critic notes the live em_item_root uses a host cast for 00128350).
  - 00102718 and 00102738: inline in em_effect_original / em_coll_*.
- **L20**: the 16 verified-unbound rows are in `em_message_service.c` and
  `em_message_draw_original.c`, neither in COMMON. The live panel message
  service reports 001FC9B0 as no-effect (um_001FC9B0). 001FCB90 is the
  stand-in row.
- **L22** (live since census L22, 2026-09-24: FIRST_LEVEL_CENSUS.md section
  1.10; below is the state before it): all 24 rows were verified-unbound. Owners:
  - `em_roger_actor_original.c` (not in COMMON): 008237E0, 001C5C90,
    001BA540..001BA8E0, 001CA5E0..001CA770, 001D0690..001D0C70,
    001B10B0, 001B1020, 001AF780, 001AF890, 001D8BF0.
  - `em_roger.c` / `em_roger_runtime.c` (in COMMON; WP-9): 00823910,
    00823950, 00823B70. Roger is unbound; today's pool node draws him
    static.
  - `em_cinematic_playback.c` (not in COMMON, H4): 0022EEF0, 0022EC30.

## 6. Limits

- None of these translations is live. The coordinator binds them (section 4).
- **001D2910 in the route runs** executes the original over the capture. The
  flag results (1, 2, 0 in all 22 captures/beats) are fed to the native
  side, so they are replayed, not independently derived.
- **Hooked callees** are scripted, not verified here:
  - L31: 001D2830, 001D52E0, 001D8FD0, 001C1EA0, 001E0CC0, 001E1E60,
    001E1AD0.
  - Model binds: 001C6120, 001CA5E0, 001C6150, 001AF780, 001CB5B0,
    001C62C0.
  - 001FCB90, 001D49D0, 001D4B10, 001D1F80.
  - The shadow chain runs everything unhooked.
- **Float model.** VU0/COP1 results come from `ee_float_model.py`
  (measured, docs/EE_FLOAT_MODEL.md); the VCLIPW compare uses host floats
  of the lane values (a pure compare).
- **Census coverage of the callers.** 001027E0 is reached only through
  00102CD0, 00102850 only through 00209280, and 001000E0 through 0017C580
  (beat 10). The test exercises these as units, not through their callers.
- **Scope.** 001DA080/001DA310/001D5C80 are verified as em_shadow_original.c
  computes them. The GS-side draws (Metal) are `test_shadow_original_reference.py`'s
  domain.
