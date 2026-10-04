# AREA01 arrival binding dependencies

Read-only mapping, 2026-10-03. The map follows the original callers and names
implementation owners. It is not a claim that any unbound path runs live.
The user-requested scope supersedes the older first-level-only instructions.

## Original frame chain

The task chain is 001ACEC0 -> 001AD250 -> 001AD4D0 -> 001AE040
(`anim_frame_top_b.c` in the decomp, translated by `em_scene_frame.c`). State
0 advances the state byte, then calls 001AFCA0, 001AFCF0, 001B07C0(0),
001B6990, 001D19E0, 001C1DC0, 00199C50, 001AEE40(4), 001FAE70(1),
001C5C50, 001D1EF0 and returns. The arrival smoke already reaches that return.

On the next frame state 1 runs 001AE7E0. With the ordinary classifier result
and no loader busy byte, it selects 001AE5E0 when scratchpad 3B8D is zero,
otherwise 001AE6B0. These original routines are byte-matched C. The gameplay
variant runs player context/0015BCF0, render setup/001C1D00, pool walk mode 0,
player shadow/draw, effects, camera context/0018B9C0, collision close-out and
presentation. The cutscene variant runs render setup and pool mode 1 before
effects/player, then pool mode 2, shadow, camera, close-out and presentation.
Use the existing `em_sf_001AE5E0` / `em_sf_001AE6B0` owners and retain that order.

The deliberate fail-stop is `em_scene_bindings.c:w_001AD4D0`: AREA01 with
frame state other than zero faults at 001AE040. Do not remove that condition
until the workers below are complete. `em_area01_arrival_bind` only records
the callback and spawn record for the exit comparison; it binds no behavior.

## Smallest complete arrival package

1. **Canonical storage and owner dispatch.** `EmActor` is a native struct,
   not an EE-byte overlay (`em_actor_pool.h`). AREA01's standalone modules
   use original addresses and original-layout bytes. New adapters must map
   those reads/writes to the canonical pool, player, progress, request,
   scratchpad, resource and bone-slot owners. The overlay has a `bytes` hook;
   math uses `EmA01Math` RAM/spad views; sys uses `EmArea01SysRegion` views.
   A second unsynchronized arena is not a live binding. Commit/reload record
   views around workers where the original calls and re-reads them. Reuse
   existing owner adapters for already-bound functions.
2. **Area state and assets.** Arrival already loads AREA01 roster, spawn,
   EMCL/cells, and static-object resource 0x44 from the module loader.
   Actually run the AREA01 overlay init (runtime 00823A50, decomp
   `func_overlay_AREA01_00823A10.c`) through the shared 001E7780 dispatch:
   its effect-grid globals and BSS storage are read by owner 001E7D20.
   Those stores are currently treated as a loader boundary. Use the
   exported overlay-data region, not capture data at runtime.
3. **Shared player and collision.** `w_001AFCA0` already binds the shared
   player stage and camera over AREA01 collision. The first 0015BCF0 after
   arrival spawns the 0015C420 player children and runs stage rebuild.
   Keep the existing player, equipment, effect and indicator owners.
   `a01_00` exercises grab/hang/pull-up/fall/land; the route's new historical
   addresses do not by themselves mean missing implementations after later
   first-level work. Check every census address against current owners.
   0019B4C0/001A06A0/0019CF50 need the sys dispatcher (see
   `LEVEL2_COLLISION.md`).
4. **Render world and shadow.** State 0 currently calls `rcl_bind` and
   `em_shadow_live_bind` only for AREA11; AREA01 inherits the old render
   bindings for arrival only. Generalize the binding to the delivered area
   bank. Calling `rcl_bind` unchanged is unsafe: its
   `em_rcl_static_world_load(NULL)` reads the AREA11 default export and can
   replace the delivered bank. Bind the AREA01 static-world VIF routines
   (`em_area01_render_vif.c`: 001D4FC0, 001D5170, 001D5A70, 001D5BD0),
   owner render hooks and player shadow to the same render context.
5. **Every pool callback, including hidden owners.** `spawn_area01` must
   use a real callback binder. Pool scheduling reaches offscreen/dormant
   owners too. Shared callback translations already live in
   `em_area11_bindings.c` include pickups, crates, 001BC350 doors, props,
   title, equipment, effects and indicator children. Reuse their underlying
   original modules; several current hosts have AREA11-only resources and
   must accept area inputs before reuse. New callback families are listed
   below. A state that frees an owner still must run its original setup and
   teardown workers in the original order.
6. **Camera.** Keep `em_camera_live` / follow / leftovers / specials. The
   AREA01 state-0 placement already binds and uses them. First-visit area 1
   has no special arm in 00195130's area switch. Camera mode 1's 001B0300
   presently faults in the host, despite its existing translation in
   `em_area01_sys.c`; bind it if reached. The cinematic host and scene-entry
   tables must become area-aware before script camera modes are enabled.

## Pool modules required before the first world frame

| Original callback or family | Existing owner / next binding |
|---|---|
| 00128C10, 00158D30, 00159B90, 0015A2C0, 001E3D90, 001E7D20 | `em_area01_sys.c`; worker dispatcher into canonical owner, animation, collision, sound and render services |
| 001BB860, 001BFFD0, 001C02E0 | `em_area01_math_owner.c`; 001BFFD0 includes the census's separate 001C0004 piece |
| 001B13F0, 001B2140, 001C25E0, 001C2770, 001C39F0, 001C3BE0, 001C3D60 | `em_area01_math_actor.c`, called by the new owners |
| 00128390, 001289C0, 00128AB0, 00129780 | `em_area01_exita.c`, already needed by arrival's 00128C10 family despite the module name |
| 001BB520, 001C2540, 001C3DB0, 001E9580 | `em_area01_exitb.c`, also needed by arrival |
| 00823580 shaft door; 00825350 NPC and 008254B0 first talk; 008261A0 with 00826200/00826440; 008267C0; 00826CF0; 00826D40 and 00828850 | `em_area01_overlay.c` / `_826d40.c`; bind its typed hooks and record mapper |
| 001CD180, 001CD2B0, 001F4A10, 001F4CC0 | `em_area01_render_gs.c` |
| 001E9E60 | `em_area01_render_hud.c` |
| 001F4BF0 | existing `em_status_scene_original.c` owner |
| 001AA000 | existing live `em_coll_list_passes.c`, not a new sys copy |

`FIRST_LEVEL_CENSUS.md` 3.26 adds 62 post-arrival functions, including
functions that the older a01 delta omitted because beat 15 had already run
them. Union that set with the a01 route delta. There are real holes in the
prepared translations:

| Missing original | Source and dependency size |
|---|---|
| AREA01 00825740 | `src/overlays/AREA01/func_overlay_AREA01_00825700.c`, 464 bytes, 113 census instructions. Another talk owner. Its first-visit setup calls 001B10B0, 001BA8E0, 001C63E0, writes descriptor/yaw/bank, then sets lifecycle 3 because 75A is zero. Next tick calls 001BA540 then frees. Same shared NPC workers as 00825350; its later talk arm uses script 82A7B0. No port implementation found. |
| 001D0C80 | `src/func_001D0C80.c`, 47 census instructions. Mode-5 model setup via 001CA5E0, bone-count 001C6150, canonical bone-slot cap/allocation 001AF780, bone-array publication, extra control record at +90. Requires the real shared bone allocator; do not assume infinite capacity. No port implementation found. |
| 001D0D40 | `src/func_001D0D40.c`, 8 census instructions. Initializes the control record pointed to by actor +90 with descriptor, float count, zero time and mode. Reuse EE integer-to-float behavior. No port implementation found. |
| 001C4FA0 / 001C50B0 | Their matching C files, 43 / 303 census instructions. Type/story predicate and flicker-light owner. Setup can immediately enter teardown; full owner uses 001F5490 / 001F5F60 already in `em_area00_fx_exit.c`, plus 001C5050, light release and random/vector workers. No port implementation found. |

These are source translations to add once, not permissions to omit owners
that are subsequently freed. Original overlay addresses must also carry the
area identity; multiple overlays use 00823580.

## Scripts, doors, pickups and messages

The first three movement beats do not start their own AREA01 conversation,
but all NPC/door setup and polling still runs. After those, a01_03 starts the
shaft-door scripts 829860 and 8298E0; a01_05 starts NPC script 829FA0.
Continue using `em_area_script` and its existing op implementations. Supply
AREA01 EMSC images/quads and bind op09 callbacks 825130/825240 to
`em_area01_overlay.c`. `em_area11_script_host` currently loads AREA11-only
images/camera resources and handles only AREA11 op09 targets; it cannot be
used unchanged. Its owner clip dispatch distinguishes only AREA11 door and
Roger, so generalize the host service input instead of copying interpreter
logic.

The shared 001BC350 door owner already exists, but
`em_area11_door.c:h_transition` refuses areas other than 0x0B and loads an
AREA11 destination row. Bind AREA01's exported door table, descriptors,
model bank and shared script host. The special shaft callback wraps these
same door workers; it sets story byte D_008107D9 to 0x80 after the locked try
and sets linked r13 +0xB each frame from D9==0x81. The NPC's 825590 completion
stores 759=FF and D9=81. Keep progress in `EmSceneState`.

`em_area11_boxes.c:load_bank` is AREA11-hardcoded, and its loaded bank is
cached. Common crate/prop/model workers need the area's `world_models` bank
before reuse. The interaction host also needs area-specific placements,
Use/class-list scan, descriptors, pickup data and player takeover hooks.
Do not bind AREA01 pickup callbacks to an unloaded AREA11 host.

Message loading supports EMMD area 1 already, and the AREA01 export exists.
The live service only has one installed area bank; `w_draw_line` faults on
an area mismatch. `em_message_live_install` calls shutdown and clears hosts,
streams and presenters, so area change needs either a preserve-service bank
switch or explicit rebinding of all three. AREA01 has 54 global / 184 area
records. Original 001FD950 remains the one draw/service owner. Keep message
state reset at the original reset sites, not at a convenient arbitrary tick.

## First route milestone

Arrival is spawn entry 4 at (41, 0, -565.6), yaw -1.43411. a01_00 takes 780
frames and exercises ledge grab (f304), hang (f357), pull-up (f360), top of
crate (f464), fall (f491) and landing (f519). a01_01 adds 305 frames through
the tunnel; a01_02 adds 590 to the shaft landing. The route remains at 100
health. Compare player, camera, pool lifecycle/state, progress, messages,
random-dependent owners and collision traces against these recordings; an
end-position-only comparison is insufficient. Binding one owner at a time
behind the AREA01 gate is safe; enabling the frame before every scheduled
owner has an implementation is not a complete arrival milestone.
