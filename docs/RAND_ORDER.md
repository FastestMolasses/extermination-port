# rand() call order: the port against the original

The game has one random-number generator, 00122BB8 (the SDK `rand()`, byte-matched
C in the decomp). Every caller draws from one state word, so the order of the calls
decides every value: the lamp's sway, the glow markers' pulse, the head sprites'
waits, the faces' blinks, the weather and the music's fade length. This document
records how the port's order was compared with the original's, what matches, what
was fixed and what still differs.

Evidence:
- the decomp's C7 per-call capture (`../Extermination/docs/CAPTURES_C7.md` section 3,
  `build/s87/c7cap/rng/<newgame|r01|r10>/rand.jsonl`);
- the port's trace;
- `tools/rand_order.py`, `make test-rand-order` and the level smoke's `check_rand_order`.

## 1. The two traces

**The original.** The C7 capture recorded every call of 00122BB8 with:
- its frame;
- the caller's return address;
- the state word before the call.

It covers three stretches, each with the call chain intact:
- **newgame:** from the NEW GAME commit to first control + 300 frames (36,512 calls);
- **r01:** route 01, f1..f516;
- **r10:** route 10, f1080..f1400.

The state is 1 at the commit, and srand is never called. The original's own order in
the opening varies from run to run after about 689 calls (AE+31): two runs agree up to
there and then differ in caller order.

**The port.** `EM_RAND_TRACE=<path>` (src/game/em_random.c; test instrumentation)
writes one line per call with:
- the main-loop counter;
- the state word before the call;
- six native return addresses, relative to the image base.

`tools/rand_order.py` resolves the addresses with the binary's symbol table (`nm`).
The first frame that is not a worker adapter names the translated function, which
maps to its original function (`PORT_FN`). The original's return addresses map to
their functions the same way (`ORIGINAL_RA`). An unknown caller on either side
fails the tool. Both traces become (original function, state) per call.

| Original caller | Where it draws | Port translation |
|---|---|---|
| 001D7C30 | the point light's sway, twice a frame | em_point_light_tick |
| 001F54E0 | each indicator child's colour pulse | em_effect_kinds_001F54E0 |
| 001F4D40 | the barrel's eleven glow markers | em_effect_manager_001F4D40 |
| 001FAE70 | the music cue's fade length | em_stream_lanes_001FAE70 (music_select) |
| 001F1110 / 001F1180 | the items' aura | em_pickup_aura_001F1110 / _001F1180, em_effect_manager_aura_draw |
| 008235F0 (0x8236B4) | the AREA11 effect owner's first tick | em_area11_effect_tick |
| 00825940 (0x8259F0) | the security gun's lifecycle 0 (its +0x28 scan timer) | em_gun_tick's gun_setup (em_security_gun, bound since census L24) |
| 001D0720 | the faces' blink, expression and mouth | em_opening_face_tick |
| 001E2560 | the head sprites' wait and scalar | em_head_sprite_original_tick |
| 001E55F0 | the weather | em_weather_tick |
| 00179B90 / 001EA240 | the footsteps | footstep_rand5 / x_step_random5, driver_seed |
| 0020A7A0 | the status pages' background pulse | em_status_background_step |

**Classes.**
- **Deterministic callers** draw on a fixed schedule: 001D7C30, 001F54E0, 001F4D40,
  001FAE70, 001F1110, 008235F0 and 00825940. The sequence of these calls in a frame
  (its "skeleton") does not depend on the values drawn.
- **The others** draw when a timer they took from an earlier draw runs out, or on
  the walk. Their frames follow the values once the two streams differ.

## 2. What was fixed in this step

**0x1AE040's 001FAE70 calls at the area entry and the room move are bound**
(em_scene_bindings.c `w_001FAE70`). Both called 001FAE70 without its effect before.
- **The area entry** (state 0, 0x1AE0CC, a0 = 1). This is the first rand() after New
  Game, from state 1: the original draws 0x41C67EA6 there. The call also issues the
  area music's read (cue 25).
- **The room move** (state 4, a0 = 0). It draws once, then cue 25 continues. The
  traced fence-door run draws at both room moves (counters 6774 and 7239).
- **Still reported (UM_001FAE70):** state 2 (0022A650 == 1) and state 6. No smoke run
  reaches them.
- **Effect on the opening.** The area music's read is now in flight when the
  opening's cue-0x3F prefill is requested, and the prefill waits for it, as in the
  original. At host speed (the default) the prefill waits one field for it; with the
  PS2 disc-drive timing switch on, the drive model's 6-field seek (section 3).
  newgame-control: locked_ticks 1301 at host speed, 1311 with the switch; the
  displacement is 9.599849 in both. The frame-order post-control window is native
  index 1393 (counter 2650) at host speed, 1665 (counter 2922) with the switch since
  chain step H7 (the New Game's loads take ticks; 1330 / 1340 before).

## 3. The New Game opening (the newgame capture)

The two traces are aligned on the area entry: port counter 1273 = original frame n270.

| Check | Result |
|---|---|
| The area-entry frame | Equal: one call, 001FAE70 from state 1 |
| Call for call (caller and state) | 227 calls equal at host speed (447 with the switch on): the area entry, AE+1's calls including the security gun's 0x8259F0 draw, Roger's owner's face at AE+2, the player's face in the player stage after the barrel from AE+5, and every call up to the opening's actors' spawn |
| First difference | The opening's actors' spawn, the frame the script's op14 runs once the stream request's wait ends: AE+10 at host speed (AE+20 with the switch), where the original, still waiting, spawns them at AE+31. In that frame the port's opening body draws its face's first values (001D0720, in the pool walk after the indicator children) where the original draws its glow markers (001F4D40), from the same state. (Before chain C8b OPENING it was the player face's AE+5 draw, missing while em_opening_actor ran the faces; before census L24 the security gun's AE+1 draw.) |
| From the spawn, caller for caller | Each frame's callers equal the original's frame 21 later (11 with the switch) for 32 frames: the body's face and the head sprite its 001BA8E0 spawned, the player's face after the barrel. Then a value-driven timer, drawn from a state the wait moved, differs first (a head sprite at the original's AE+63) |
| Deterministic skeleton, frame for frame | Equal in every frame from AE+1 to the port's first control (AE+1302 at host speed, AE+1312 with the switch), the security gun's AE+1 draw included |
| The 30 frames after first control | Equal |
| The opening's end | Port AE+1303 at host speed (the default) against the original's AE+1324: exactly 21 frames earlier. AE+1313 (11 earlier) with the PS2 disc-drive timing switch on |

**Why the opening ends earlier.** The opening hands over control when its stream
request (cue 0x3F, with the D_008106F4 hold) keys on. In the C7 stream capture of the
opening that request waited 21 fields on the drive: 15 extra fields of ready query
while the area music's read finished its 16-field seek (the first stream read after
the New Game's module-loader reads; IOP_STREAM.md "Drive model"), and 6 extra fields
of read. At host speed (the default; disc timing is not
part of the Original profile, CLAUDE.md 2026-09-27) the port's request reads at once,
so first control comes exactly those 21 frames earlier, which check_opening requires
(LEVEL_SMOKE.md "The stream drive's two modes"). With the switch on, the drive model
serves the area music's read as a first read (a 6-field full seek), the request waits
10 fields, and first control comes 11 frames earlier (allowed up to 16).

**The value-driven callers over the opening** (reported, not asserted):

| Caller | Original | Port | Why |
|---|---:|---:|---|
| 001D0720 faces | 712 (460 in the pool walk, 252 after the barrel) | 717 (463 in the pool walk, 254 after the barrel) | the values differ after the spawn |
| 001E2560 head sprites | 35 | 35 | the values differ after the spawn |
| 001E55F0 weather | 23 | 28 | the values differ after the spawn |

**The faces.** In the original's opening, and in the port's since chain C8b
OPENING (OPENING_ORIGINAL.md):
- Roger's owner 008237E0 ticks his face in the pool walk (its first values at AE+2).
- The player's face ticks in the player stage, after the barrel: 0015BA50, then
  00183090 under 3B8F == 2, then 001D0C70, from AE+5.
- The opening body (the script's class-9 001BB0E0 record) ticks its face in the pool
  walk from its spawn, after the indicator children; the head sprite its 001BA8E0
  spawned draws right after it.

## 4. Gameplay: the route windows

The level smoke aligns two phase windows row for row with route stretches the
capture holds. check_rand_order compares every frame of those windows.
- **01 (the battery take, 66 frames):** every frame's skeleton is equal.
- **10 (the director's beat 0 with Roger, 311 frames):** every frame's skeleton is
  equal.

The value-driven callers sit in the same places as in the original, with counts that
follow the values:

| Window | Original | Port |
|---|---|---|
| 10 | faces 172 in the pool walk and 13 after the barrel; head sprites 6; steps 2; weather 1 | faces 144 in the pool walk (Roger's owner) and 16 after the barrel (the player's face slot in the scripted frames); head sprites 7; steps 2 |
| 01 | head sprites 2; faces 1 | head sprites 1; faces 2 |

The status background 0020A7A0 draws once in the port's run, at its 381st status-like
frame: layer 1's pulse ends after about 360 calls. No capture holds a status frame
that far into the pulse, so its position in the frame is not verified.

## 5. Making the rand()-driven state comparable

The port reaches each route snapshot from its own New Game run, so its stream is
never at the capture's position there. The original's own order also varies from run
to run. So the values themselves cannot be equal. Instead the smoke compares each
rand()-driven value as the original's code applied to the same draws.

**The lighting rows' fold lane** (check_owner_units). The ORIGINAL 001CAA00 runs a
second time over each snapshot. It uses the port's point-light pool, which the tick
log's `lights` rebuilds byte for byte; its slot digest must equal the one the port drew
with. It also uses the port's view D_00810610 as the draws read it: the camera stage
commits the next view after the draws, so the draws use the tick before's.
- **Result:** the whole lighting rows equal the port's for every placed owner,
  including the player and the equipment at the camera-exact 10 and 14.
- **Before:** the rows were compared for 0 units.
- **The view:** at 10 and 14 the port's committed view equals the capture's in all
  16 words.

**The sway itself** (check_sway). On sampled ticks the ORIGINAL 001D7C30 runs over the
port's previous pool with the port's own two draws of the frame. It writes exactly the
port's pool (46 ticks on the full route, 6 of them snapshot ticks).

**The glow markers' colour** (check_marker_colour).
- **The port.** On sampled barrel frames, each of the eleven 001F4D40 calls drew the
  value the trace gives it, and the ORIGINAL 001F4D40 over its colour words and that
  value hands 001CD520 the port's rgb (506 calls).
- **The capture.** In the camera-exact snapshots, the capture's own draws are the
  frame's last eleven calls. They are recovered from the snapshot's state word
  (*D_0024295C + 0x58) by stepping the LCG back. Each captured marker's colour is the
  depth fade of the original 001F4D40's rgb for that draw, with the same fade factor
  as the port's marker. So the two differ only by the draw: 5 markers at 10, none
  visible at 14.

**The head sprites** (check_head_sprites). Every tick's sub-state +0x05, wait +0x1F0,
ramp +0x244 and scalar +0x24C follow 001E2560's transitions, over the port's own
001E2560 draws in pool order:
- a new sprite: wait = v % 40 + 60;
- the flip to the ramp: ramp 0, scalar = v / 2^31 (EE cvt.s.w and div.s);
- the ramp's end: 1.5 and a new wait;
- otherwise the wait counts down or holds, and the ramp adds 0.02 (EE add.s) or holds.

test_head_sprite_reference proves the translation's transitions against the original
instructions.

**Mutation checks.** Each check fails on a mutated copy of the logged data:
- a dropped sway draw in an opening frame (the skeleton);
- a missing area-entry draw;
- a changed sway draw;
- a changed marker rgb;
- a changed head-sprite scalar;
- a changed player lighting-row digest at snapshot 10 (the fold lane).

## 6. What still differs (for the lead)

1. **Done (census L24): the security gun 00825940.** Its lifecycle-0 draw at AE+1
   (0x8259F0) now runs on its own record (SECURITY_GUN.md 5.1); the opening is equal
   call for call up to the faces' divergence below.
2. **Done (chain C8b OPENING): the faces in the opening.** The player's face ticks
   at the player stage's 00183090 from AE+5 and the opening body's in its own record
   from its spawn (OPENING_ORIGINAL.md). The first difference is now the spawn frame
   itself: the drive's timing (item 3).
3. **The opening's actors' spawn and its end** come 21 frames earlier at host speed
   (the policy) and 11 with the switch (disc timing, section 3). With the
   drive-timing switch (LAUNCHER_OPTIONS.md) it would need the original's 16-field
   seek after the New Game's last module-loader read (explained since 2026-10-09:
   IOP_STREAM.md "Drive model"; modelling it under the switch is an open decision).
4. **The status background's draw** has no capture of its frame position.
5. **Run to run.** The original's own opening order diverges after AE+31. A value
   comparison past the first divergence needs the substitution method of section 5,
   not a longer trace.

## 7. Binding

- **`em_random_next`** stays the one state. Every live caller reaches it through its
  module's worker adapter. `tools/rand_order.py` PORT_FN names them. A new rand()
  caller must be added there with its original function, or the smoke and
  test-rand-order fail on it.
- **The trace.**
  - `EM_RAND_TRACE` is headless, like every trace variable (em_platform.h).
  - main.c registers the clock (`em_random_trace_clock(em_frame_counter)`).
  - The fixtures that link em_random.c on its own write counter 0.

## 8. Running it

```
make test-rand-order        # New Game to first control + 30 with EM_RAND_TRACE; about 10 s
make test-level-smoke-full  # check_rand_order, check_sway, check_marker_colour, check_head_sprites
```

The smoke target writes `build/level_smoke/rand.trace` next to the tick log.
