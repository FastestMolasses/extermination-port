# SDK VU0 leaves: one translation each

`src/game/em_sdk_vu0.h` holds the one port translation of three SDK VU0 leaves
of the boot ELF that many translated modules call. Before it, five modules
carried private copies of 001026D0 / 00102900 (SHADOW_ACTOR_ROUTE.md L5) and
many carried their own 00102948. Census section 1.28 records the reduction.

| Original | What it does | Decomp file |
|---|---|---|
| 001026D0(dst, a, b) | 4x4 product. The four rows of `a` are loaded first. Then each row v of `b` is loaded and gives dst's row: ACC = a0 * v.x, ACC += a1 * v.y, ACC += a2 * v.z, row = ACC + a3 * v.w (all four lanes). Each row is stored before the next row of b is loaded. | hand-written asm |
| 00102900(dst, v, s) | dst = v * s in all four lanes. The float argument is moved into the x lane of the second operand, and only that lane is broadcast. | hand-written asm |
| 00102948(dst, src) | Copies one quadword: all four words are loaded, then stored. | hand-written asm |

## Form

- **Header-only.** The header uses `static inline` functions, like
  `em_ee_float.h`. It depends on nothing but `em_ee_float.h`, so every caller
  and every caller's test uses this translation without linking another module.
  This is what kept the copies alive before: linking em_locomotion_display
  pulled pose-host dependencies into the shadow route's tests.
- **Arithmetic.** Every VU0 instruction goes through `em_ee_float.h`'s
  measured macro model (EE_FLOAT_MODEL.md). 001026D0 and 00102900 return the
  model's status, `EM_EE_FLOAT_OK` or the nonzero refusal. A caller turns a
  refusal into its own fault at the leaf's address.
- **Aliasing** is the original's: dst may be a or b (001026D0), v (00102900)
  or src (00102948).

## Callers

001026D0:
- em_shadow_actor_route (001F8D30)
- em_locomotion_display (00179FF0, 0017B660)
- em_equipment_live (the equipment's w_001026D0)
- em_effect_manager (001F6210's two products)
- em_status_models (w_001026D0 over the scratchpad matrices)
- em_frame_render_heads (001D2960's K and alternate products, over its memory
  views)
- em_shadow_original (001DA6A0's products). This one used a host-double
  truncation model before. The full shadow oracle sweep (6,144 area keys, 192
  clip batches, 15 route beats) and the level smoke's check_shadow pass on the
  measured model.

00102900:
- em_shadow_actor_route (001F8D30's colour)
- em_actor_light_001D89D0 (001D8340's fold)
- em_camera_leftovers (`cl_v_scale4`)

00102948:
- em_shadow_actor_route (0015BF90)
- em_effect_manager (001F0720's fog quadword)
- em_actor_light_001D89D0 (001D8340's three copies)
- em_camera_leftovers and em_camera_commit_original (`cl_v_copy`)
- em_frame_render_heads (`copy_qw`, over its memory views)
- em_anim_runtime_rest (001CB2C0, 001CAAC0)
- em_player_ladder_entry (`copy4`)
- em_camera_area11_specials (`copy16`)
- em_area_script (`quad`)
- em_script_host_workers (`qcopy`)
- em_camera_follow_original, em_player_misc_workers, em_player_recovery,
  em_player_climb, em_player_running_jump, em_effect_original and
  em_load_veil_particles: the host quadword copies their comments cite as
  00102948.

Sites where the copy is a load and a store through a module's own accessors
for an original record or address still copy through those accessors. The
leaf would add nothing between the accessor's load and store. These are:
- the record helpers of em_player_closure_0e_18 and em_player_closure_10_12_19
- em_player_running_jump's and em_player_recovery's +0xB0 loops
- em_script_door_fan_husk's field loops
- em_player_equipment's `spad_put`
- em_render_context's `ldq`

## Not reduced (for the lead)

- **em_crate_original's `em_crate_sdk_multiply` (001026D0)** keeps its own
  arithmetic. Its oracle, test_crate_original_reference, runs the SDK
  routines with the ITEM SDK oracle's semantics, not the measured VU0 model.
  The measured leaf differs from that oracle on at least one synthetic case:
  a result lane of 0 where the oracle gives the negative denormal 0x800005D0.
  VU0 flushes denormals, so the oracle's value is probably not what the
  hardware produces. Binding the crate to the leaf first needs its oracle's
  SDK calls moved onto `tools/ee_float_model.py`. Because the crates and
  drums run live, this is a fidelity question as well as a duplicate.
- **em_snow.c's tile colour (00102900)** is an inline scale on em_snow's own
  host float model. test_snow_tiles_reference models the leaf in Python
  instead of executing it. Moving it onto the leaf means moving that model
  onto `tools/ee_float_model.py` as well.
- **001026A0 and 00103230** have several copies, for example
  em_effect_original's `sdk_apply`, em_status_models' `vu_row`,
  em_locomotion_display's `em_loco_00103230` and em_coll_list_passes_walkers'
  `sdk_scale`. They were outside this step's contract.

## Evidence

- `tools/test_sdk_vu0_reference.py` (`make test-sdk-vu0-reference`) executes
  the ORIGINAL 001026D0, 00102900 and 00102948 from the user's ELF. It covers
  every special value in every lane, the three aliasing forms of 001026D0 and
  the aliasing form of the other two, and a random sweep: 300 cases quick,
  3,000 full. It compares the whole guarded window, so the destination and
  the words around it are both checked. Two mutations were confirmed to fail
  it: skipping the load of `a`, and a 12-byte copy.
- Every caller keeps the oracle it had, now running the shared leaf:
  - test_shadow_actor_route_reference
  - test_locomotion_display_reference (its 001026D0 leaf sweep moved to
    test_sdk_vu0_reference)
  - test_effect_manager_reference
  - test_actor_light_001d89d0_reference
  - test_camera_leftovers_reference
  - test_status_scene_reference
  - test_frame_render_heads_reference
  - test_player_equipment_reference
  - test_shadow_original_reference
  - test_render_verify_rest_reference
  - the level smoke
