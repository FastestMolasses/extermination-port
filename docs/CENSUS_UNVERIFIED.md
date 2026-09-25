# The seven unverified census rows, checked against the original

Date: 2026-09-24. Scope: the seven rows `docs/FIRST_LEVEL_CENSUS.md` (recount
2026-09-24) marks **unverified**: 0015AC00, 0015CF90, 001B1190, 001C5680,
001C5760, 001CF470 and 0020DFA0.

Test: `tools/test_census_unverified_reference.py`. The default run takes about
5 s; `EM_TEST_FULL=1` takes about 11 s. The test runs each original routine
from the pinned ELF on the measured EE float model. It uses the render lane's
`RvrEE` interpreter, with the VU0 absolute value and two MMI word shuffles
added for 001CF470. The inputs are synthetic records plus the captured RAM of
route beats 00..14. The results are compared with the port code as it stands;
no port file was edited:

- em_pickup.c and em_status_models.c are compiled whole inside host
  harnesses. The harness `#include`s the module, so its static functions are
  reachable. The harness supplies only the storage getters (`em_scene_state`,
  `em_random_next`, `g`). Any other callee is linked as a trap that aborts
  if it is reached.
- The indicator children run em_indicator_child.c and em_effect_kinds.c
  (the one 001F54E0 translation) as the game links them; the spawn colours
  are read from em_area11_bindings.c's source.
- Two live pieces sit inside functions that cannot run alone:
  - the 0015CF90 lines of `em_player_0015BCF0`;
  - the two 0015AC00 switches of `em_area11_interaction_host_pickup_state0`.

  For these, the harness copies the exact source lines, found by their text,
  and compiles them against the real headers. If the text moves, the test
  fails.

The test pins every known divergence (`EXPECTED`). It fails when a new
divergence appears, and also when a pinned one disappears. When that happens,
update this file and the table. The build directory
`build/census_unverified_reference/report.json` holds the counts, the
examples and the source hashes.

Each pinned key covers only the exact cases it names. Any other mismatch goes
to an unpinned key and fails the run. This was checked by mutation, on
scratch copies of `src/` (the live tree was not touched):

| Mutant | Result |
|---|---|
| em_player_frame.c `<= 0.0f` → `< 0.0f` | fails: `0015CF90/synthetic` |
| the B9 latch check deleted | fails: `0015CF90/synthetic` |
| em_pickup scales model 0x72 by 2.0 | fails: `0015AC00/capture-scale`, `0015AC00/scale-219550-unscaled-model` |
| em_indicator_child: +4 = 4 does not free | fails: `001C5680/child`, `001C5760/child` |
| the husk child's spawn colour w 0.25 → 0.5 | fails: the spawn-colour capture check |
| fix 3 (`em_ee_c_le`) applied | fails: only `0015CF90/c.le-daz` reported gone |
| only the 0020E020 part of fix 2 applied | fails: only `0020DFA0/missing-0020E020` reported gone |

## Summary

| Row | Verdict | First-level impact | Fix for the chain |
|---|---|---|---|
| 0015AC00 | **verified** (scale switch, 001F1110 variant); two boundaries | none today | optional: 00219550 items keep scale 1.0 |
| 0015CF90 | **verified** except the compare model | none reachable (denormal / negative-NaN health) | use `em_ee_c_le` |
| 001B1190 | **verified** (areas 0..0x16, capture 00→01) | none | none needed for AREA11 |
| 001C5680 | **verified**, live per node (em_indicator_child) | none: every child draws its 001F54E0 in walk order | the 0x7A child's own model draw (OWNER_DRAW.md P1) |
| 001C5760 | **verified**, live per node; the terminal's colour tail 0x827EAC is live | none (the arrow turns green once powered, as in route 04) | none |
| 001CF470 | **missing**: no port translation exists | the actor-route decal is not drawn | translate 001CE300 + 001CF470 |
| 0020DFA0 | D_00810610 writes and zoom **verified**; four callees not run (one key each) | trail reset on the request path; fog save/program | see the 0020DFA0 section |

## 0015AC00 (the 0015AFA0 owner's state 0)

**Where the live code is.** The census module column names only em_pickup.c.
The live INIT for AREA11's 0015AFA0 owner is in `em_area11_interaction_host_pickup_state0`
(em_area11_interaction_host.c). It holds the scale switch on +0x0D and the
001F1110 variant switch on +0x03 & 0xF. em_pickup.c `pickup_model_scale` is
the draw-side copy of the scale.

**Checked.**
- The original ran over every +0x0D (0..255) and eight +0x03 bytes, with the
  model-bind result 0 and 1. The full run gave:
  - em_pickup's `pickup_model_scale("props/item_XX.emdl")` equals
    +0x60/+0x64/+0x68: 4,096 of 4,096;
  - the host's scale equals +0x60: 4,096 of 4,096;
  - the host's variant equals the original's 001F1110 a1: 2,048 of 2,048.
- The original's write set was confirmed:
  - +0x60..+0x68;
  - +0x80..+0x88 = 4.0 when the nibble is 1;
  - +0 = 1, +8 = 3, +0x30 = 0x275488;
  - the call order init → 001C6380 → 001F1110.
- In the capture, for every AREA11 item owner in beats 00..14 (91 records),
  the em_pickup scale of its manifest model equals the captured +0x60.

**Divergences.**
1. `0015AC00/early-return` (a boundary, not a defect). When 001B0FD0 or
   001B1020 returns nonzero (the bone-slot stack is exhausted), the original
   returns 1 before 001C6380, the +0/+8/+0x30 stores and 001F1110. The actor
   stays in state 0 and retries on the next frame. The host has no such path.
   That follows the port's convention that every slot request succeeds
   (em_area11_bindings.h).
2. `0015AC00/scale-219550` (latent). The original 00219550 state 0 was run
   whole, with its callees recorded. It never stores +0x60..+0x6B, so the
   001AFA90 value 1.0 stays. `em_pickup_add` still applies the 0015AC00
   switch to every non-prop item, so an 00219550 item whose model is in the
   2.0 group (0x40, 0x41, 0x42, 0x45, 0x4D..0x4F, 0x55..0x57, 0x59, 0x6C,
   0x6D) or is 0x5B would be drawn scaled. Every AREA11 00219550 item is
   model 0x72 (scale 1.0), so nothing is visible today. The key is pinned
   only for those scaled models. For any other model (0x72 among the
   synthetic cases) the port must give 1.0; a mismatch there is filed under
   the unpinned `0015AC00/scale-219550-unscaled-model`.
   **Fix:** in `em_pickup_original_bind`, before its `pickup_build_palette(p)`,
   set `p->scale = 1.0f` when `record->callback == 0x00219550u`.

**Not modelled, no reader in this row.** The +0x80..+0x88 = 4.0 store
(nibble 1; the captured map owner uid 0x0B09 holds it) has no port field. Its
reader is outside this row.

**Suggested census status:** live, with the module column naming
`em_area11_interaction_host_pickup_state0` and em_pickup.c's scale.

## 0015CF90 (the player's vitals copies)

**Checked.**
- The synthetic cases cover 17 health bit patterns × 5 +0x234 bytes × 4 B9
  values. D_00810707 and D_008106B9 are equal in 325 of 340 (full run). The
  15 unequal cases are listed below.
- In the capture, the player record 0x8102B0 of every beat 00..14 was checked
  (15 of 15). The capture is self-consistent: D_00810707 equals +0x234 and
  D_00810858 equals +0x220.
- The original stores exactly D_00810706/707 and D_00810858/85C, plus
  D_008106B9 when the latch is taken. The port keeps writing only 707 and
  B9. em_player_frame.c already says the other three are not canonical.

**Divergence** `0015CF90/c.le-daz`. The port tests `g.status.health <= 0.0f`
natively. The EE compare reads a denormal as zero and a NaN pattern as a
large finite number. The latch differs for these +0x220 patterns:
- 0x00000001 and 0x007FFFFF (positive denormals): the original sets B9, the
  port does not;
- 0xFFC00000 (a negative NaN pattern): the original sets B9, the port does
  not.

The key is given only to a case with one of these three patterns, B9 = 0,
original B9 = 1, port B9 = 0 and D_00810707 equal. The test also asserts
that the set of diverging (health, B9) pairs is exactly these three patterns
with B9 = 0. Any other mismatch goes to the unpinned `0015CF90/synthetic`.
The default run keeps +0.0, -0.0, -1.0, the smallest normal and the three
patterns under every B9 value, so a wrong compare or a dropped latch check
fails there too.

Neither pattern arises when every health change goes through the EE float
model. The line is still a native compare in a translated routine.

**Fix:** in em_player_frame.c, use
`if (em_ee_c_le(g.status.health, 0.0f) && scene->req[EM_SCENE_REQ_B9] == 0)`
and add `#include "game/em_ee_float.h"` (the file does not include it).

**Order note (reading, not measured).** The port runs these lines at the
end of `em_player_0015BCF0`. That is after `actor_update` (0015CBA0, 00187350
and the -200 check) and after the legacy `player_struggle_tick`. The original
runs 0015CF90 before 0015CBA0. 0015CBA0 writes only +0x230, so the order
matters only if the legacy struggle tick changes health or +0x234 in the same
frame.

## 001B1190 (taken bit, the owner's PERSIST event)

**Checked.**
- The synthetic cases cover areas 0, 1, 0x0B, 0x16 × puid 0..255, with the
  argument equal to the owner's uid byte, as em_pickup_owner passes it. The
  taken bits match in 1,024 of 1,024, including puid 0, where neither side
  writes.
- A read-modify-write keeps the other bits.
- In the capture: beat 00 plus a PERSIST of uid 0x0B01 gives exactly beat
  01's D_00810860..D_00810B3F. The port, the original and the capture agree
  (the battery take).
- The binding precondition holds. The port keys the area on the manifest
  uid's high byte, and the original keys it on D_00810700. In AREA11 every
  manifest uid has area 0x0B, and D_00810700 is 0x0B in all 15 beats.

**Divergence** `001B1190/area>0x16` (latent). `taken_byte` refuses areas
above 0x16: it reports the uid once and does not persist it. The original
writes D_00810860 + area × 32 for any D_00810700, for example area 0x17
writes D_00810B40. This cannot happen in the first level. No change is
needed for it.

**Suggested census status:** live (verified).

## 001C5680 and 001C5760 (the indicator children)

**Live path (render + UI step, 2026-09-25).** Every 001C5680 / 001C5760
node runs its own behaviour, `em_indicator_child_step`
(src/game/em_indicator_child.c), from em_area11_bindings.c
`tick_indicator`, in walk order:

- +4 = 0: 001C2360 / 001C22A0 (the model and bone-slot bind; the +0x4C
  method is 001CACB0, 001CA5F0 mode 2), 001C6380, +4 = 1; nothing drawn.
  The live workers for the bind and the placement are stand-ins (see
  "Stand-ins" below).
- +4 = 1: +0x80 = +0xA0, 001C5760 with +0xA != 0 runs 001C6380, then
  001F54E0 through `em_effect_kinds_001F54E0` (one 00122BB8 value, the
  flickered +0x80), whose +0x4C call queues the draw of the owner's child
  mesh: `em_pickup_light_submit` (00219550's 0x73), `em_props_indicator_submit`
  slot 0 (00159210's 0x75) or slot 1 (00827B10's 0x10). The draw converts
  +0x80 with 001D8C30 mode 1 (`em_effect_color_gs`).
- any other +4: 001AFC10.

The +0xA0 vector is the spawn's: 00219550 (0, 1, 0, 0.25), 00159210 (1, 0,
0, 1) (model 0x2C: (0, 1, 0, 1)), 0x825940's inline spawn at 0x825A74
(0, 0, 0, 0.25), 00827B10's inline spawn at 0x827BD8 (1, 0, 0, 0.25). The
terminal rewrites its child's +0xA0 every state-1 frame after its level step
(em_elevator_tick): `em_indicator_00827B10_colour` (0x827EAC..0x827FE8),
(0, level / 128, 0, 0.25) or (1, 0, 0, 0.25) at level 0, with 0x70003A20 =
level / 128 written into the player closure's copy of that word
(EmPlayerLandScratch.s3A20; the port keeps other copies in em_camera_live,
em_camera_leftovers and em_area11_roger, which this store does not reach;
whether an original reader takes the tail's value before writing the word
itself is not measured). The panel's completion
(00159210 state 1 / sub 2) writes +4 = 3 on its child and clears its +0x20
slot (`em_area11_bindings_panel_child_stop`) only when the slot is nonzero,
as the original does: decomp src/func_00159210.c case 2 reads r = +0x20 and
skips both stores when r == 0 (a refused 001C5570 alloc stores 0 there). An
item owner's take writes +4 = 3 on its light child.

The old path is gone: the two aggregates (`em_pickup_lights_tick`,
`em_props_indicators_tick`) ran at the first child's node and stopped for
good once that child (the battery's light) was freed: the free did not hand
the aggregate on, so from the battery pickup on no indicator ticked again
and the terminal arrow kept its last red colour after the panel powered it
(the original turns it green, route 04's screenshot). Its second copy of
001F54E0 (`em_effect_delta`, not bit-exact, EFFECT_KINDS.md 3.3) is deleted.

**Checked** (the original child ticked frame by frame, 001F54E0 recorded
with the colour quadword at its a1), against em_indicator_child_step with the
same inputs:
- 33 + 11 single-child runs of 6 frames for each kind (0x73, 0x75, 0x7A,
  0x10): steady; +4 = 3 before the first tick, after one draw and after
  three; +4 = 2, 4, 0x80 (free), 0 (re-initialize) and 1 (keep drawing); an
  init refused twice by the bone slots; +0xA = 1. Every event (init, place,
  draw with its colour words, free) equal, frame by frame.
- 001F54E0, run whole, draws exactly one 00122BB8 value and calls the child's
  +0x4C method once (0x73, 0x7A and 0x75 colours).
- The parity of 0x70003B68 changes no byte outside the stack and no 001F54E0
  argument.
- The bindings' spawn colours equal the captured +0xA0 of each kind in beat
  00.
- **Every beat 00..14 (15 of 15):** one whole frame of every child in the
  captured pool, in walk order: the original behaviours' 001F54E0 draws
  equal the per-node steps' draws in count, kind, order and colour words,
  the 0x7A child included, with the port's colours (the spawn vectors, and
  the terminal tail over the captured +0x28 level for the 0x10 child).
- A structural guard: both binding rows run `tick_indicator`, which calls
  `em_indicator_child_step`; otherwise `001C5680/binding-changed` (unpinned)
  fails the run.
- The harness runs em_indicator_child_step with its own workers (including
  a refusing init), so it does not check the live binding's workers. A
  second structural check reads them from em_area11_bindings.c and pins the
  stand-ins below.

**Stand-ins (pinned).** The live binding's workers for two callees are not
translations:
- **001C2360 / 001C22A0, the bind:** `indicator_init` always returns 0. The
  original returns nonzero when the bone slots are exhausted, and the child
  then stays in state 0 and retries on its next call. The translations exist
  (em_render_verify_rest `em_rvr_001C2360` / `em_rvr_001C22A0`,
  verified-unbound), but binding them needs model bank D_0028A56C (for
  001C2360; not exported, and em_area11_boxes faults on its 001C6120 rebind)
  and a bone-slot model for the children. Pinned as
  `001C5680/live-init-stub` and `001C5760/live-init-stub`.
- **001C6380, the placement:** `indicator_place` does nothing. The draw
  places the child mesh with the owner's current palette (em_pickup /
  em_props). 001C5760 with +0x0A != 0 re-runs 001C6380 before every draw;
  no AREA11 spawn sets +0x0A, so that path is latent. The translation is
  live elsewhere (em_owner_services_001C6380, used by the boxes and the
  status models), but it needs the bone slots above. Pinned as
  `001C5680/live-place-stub` and `001C5760/live-place-stub`.
- **The draw's cull:** 001CACB0 tail-calls 001CABA0, which calls 001CA7B0
  (decomp src/func_001CABA0.c). The port's submit does not model that cull.

These pins fail the run when the workers change, so a binding that retires
them must also update this section and EXPECTED.

**Live proof.** The level smoke passes with the per-node children (all its
capture checks row for row); the elevator phase's end frame
(`EM_LEVEL_SMOKE_PHASE_CAPTURE=elevator:<file.bmp>`, LEVEL_SMOKE.md) shows the
green arrow that route 04's original.png shows (it was red before).

**Left:** the 0x7A child's own model draw. Model 0x7A (bank D_0028A56C) has
no port mesh; its colour (0, 0, 0, 0.25) gives 1 / 128 of the texel through
001D8C30 mode 1. Its 00122BB8 draw is made; the draw itself waits on the
object-unit draw (OWNER_DRAW.md P1) and is reported once per session. The
husk owner 0x825940 (census L24) rewrites the child's colour in its later
states; the port's husk is still the legacy aggregate, so on the route the
child keeps its spawn colour, as every captured beat shows.

**Census status:** 001C5680 live, 001C5760 live, both with the stand-in
note above (the bind always succeeds, the placement is the owner's palette,
001CABA0's 001CA7B0 cull is not modelled).

## 001CF470 (the frustum clipper)

**No port translation exists.** Nothing in `src/` defines it.
- em_shadow_actor_route only records its caller 001CE300 as a worker
  (`EmShadowActorRouteWorkers.submit`). SHADOW_ACTOR_ROUTE.md L1 says
  001CE300 is not translated.
- The census module column ("em_shadow_actor_route") and its status are
  therefore wrong: the row is **missing**.
- The 001CE300 row, marked verified-unbound "em_shadow_actor_route —
  test_shadow_actor_route_reference", is also **missing**: that test only
  records the call.

**Reference recorded for the future translation.** The original runs whole
(its callees 001CF870, 001CF970 and block_copy are original too) over beat
02. It clips triangles around the player at radii 4, 40, 400 and 4,000
against the ctx+0x2240 matrix. The output fan sizes are:

| Run | Triangles | Fan sizes (size: count) |
|---|---|---|
| default | 40 | 0: 22, 3: 10, 4: 7, 6: 1 |
| full | 400 | 0: 214, 3: 88, 4: 71, 5: 21, 6: 6 |

The test pins both histograms and the first 128 bits of both SHA-256 fan
digests (`CF470_REFERENCE`), and fails if the oracle drifts. The whole
digests are in report.json.
- default: `de32ba29d0d16da2dd826454bcb73b8f…`
- full: `fbd75a9b75b6899bc21993c4958841ca…`

A translation can be checked against the same cases.

**Fix:** translate 001CE300 and 001CF470 (lane L29-shadow-route / its GS
packet work). Until then, the decal worker must keep faulting.

## 0020DFA0 (the status page CONFIGURE)

**Checked.**
- The original runs whole over beat 01, with its callees recorded. It calls
  001AFE60, then 0020E020, then 0021BAC0(0), then 0021B9A0(5, 0.0, 1e6),
  then 001D2610(0.0). It stores only D_00810610..D_0081064F.
- `em_status_models_configure` gives the same 16 words: the identity, with
  D_00810624 = -1.0.
- 001D2610(0.0), run whole (0011E398 and 001B0070 included), stores the zoom
  at 0x70003B60 and ctx+0x2468. The host's `g.cam.zoom = 0x1.e05e9ep+8f`
  equals it bit for bit.
- 001D2610 then calls 0021B970(ctx+0xF8, ctx+0xFC), which in beat 01 is
  (-209.0, 304.0).

**Divergences**, one key per callee: `0020DFA0/missing-0020E020`,
`0020DFA0/missing-0021BAC0`, `0020DFA0/missing-0021B9A0` and
`0020DFA0/missing-0021B970`. A partial fix retires only its own key. The
host's CONFIGURE case (em_area11_interaction_host.c `status_page_event`)
runs only 001AFE60 (`em_status_models_clear`), the D_00810610 writes and the
zoom; the test asserts those three are present. Nothing on the CONFIGURE
path runs:

- **0020E020**, the trail D_00821300/D_00275C90 reset. The port resets the
  trail on the hub's first frame (phases 1/2, step 0) and in em_item_root's
  state 0. A request-opened page, such as the battery take's BATTERY page,
  enters phase 3 with `item.state = 2` and skips both. So on that path a
  trail left over from an earlier status session is not cleared. (This is
  from reading, not measured.)
  **Fix:** in em_status_runtime.c `page_worker`, add a
  `case EM_STATUS_PAGE_CONFIGURE` that calls
  `em_item_trail_reset(&runtime->trail)` and then forwards to
  `hooks.page_event`. Today CONFIGURE reaches the host through
  `page_worker`'s `default` forward. The test looks for the reset in that
  case or in the host's CONFIGURE case.
- **0021BAC0(0)**, which saves the fog block ctx+0xA0 into slot 0 at
  ctx+0x120. A verified translation, `em_sul_0021BAC0`, exists but is
  unbound. The page's restore partner 0021BAE0 is a stand-in row.
- **0021B9A0(5, 0.0, 1e6)**, the fog programmer. Its census row is missing:
  there is no translation.
- **0021B970(ctx+0xF8, ctx+0xFC)**, the fog range 001D2610 re-sets. A worker
  exists in em_frame_render_heads.

The status models draw with `em_gfx_fog_off`, which stands in for the last
three.

**Suggested census status:** live for the D_00810610 writes and the zoom
constant, with a stand-in note for the four callees above. Its module column
should name the host's CONFIGURE case.

## Census corrections this implies

- 001CF470: unverified → **missing** (module: none).
- 001CE300: verified-unbound → **missing**. Its only test records the call.
- 0015AC00: unverified → live. Module: `em_area11_interaction_host_pickup_state0`,
  plus em_pickup.c's scale.
- 0015CF90: unverified → live, once the `em_ee_c_le` fix lands. It is
  verified for every reachable input already.
- 001B1190: unverified → live.
- 001C5680: unverified → live with a stand-in note (the missing 0x7A draw;
  the bind and placement workers; the cull).
- 001C5760: unverified → live with a stand-in note (the bind and placement
  workers; the cull).
- 0020DFA0: unverified → live with a stand-in note (0020E020, 0021BAC0,
  0021B9A0 and 0021B970 not run).
