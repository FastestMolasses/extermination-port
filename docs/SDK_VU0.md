# SDK VU0 leaves: one translation each

`src/game/em_sdk_vu0.h` holds the one port translation of four SDK VU0 leaves
of the boot ELF that many translated modules call. Before it, five modules
carried private copies of 001026D0 / 00102900 (SHADOW_ACTOR_ROUTE.md L5) and
many carried their own 00102948. Census section 1.28 records the reduction.
00102738 joined in the EE-float harmonization step (census section 1.29),
replacing ten private copies and the crate's host-float one.

| Original | What it does | Decomp file |
|---|---|---|
| 001026D0(dst, a, b) | 4x4 product. The four rows of `a` are loaded first. Then each row v of `b` is loaded and gives dst's row: ACC = a0 * v.x, ACC += a1 * v.y, ACC += a2 * v.z, row = ACC + a3 * v.w (all four lanes). Each row is stored before the next row of b is loaded. | hand-written asm |
| 00102900(dst, v, s) | dst = v * s in all four lanes. The float argument is moved into the x lane of the second operand, and only that lane is broadcast. | hand-written asm |
| 00102948(dst, src) | Copies one quadword: all four words are loaded, then stored. | hand-written asm |
| 00102738(a, b) | The xyz dot: t = b; t.xyz = a.xyz * t.xyz (VMUL.xyz); t.x += t.y; t.x += t.z (VADDy.x, VADDz.x); returns t.x (in f0). Stores nothing. | hand-written asm |

## Form

- **Header-only.** The header uses `static inline` functions, like
  `em_ee_float.h`. It depends on nothing but `em_ee_float.h`, so every caller
  and every caller's test uses this translation without linking another module.
  This is what kept the copies alive before: linking em_locomotion_display
  pulled pose-host dependencies into the shadow route's tests.
- **Arithmetic.** Every VU0 instruction goes through `em_ee_float.h`'s
  measured macro model (EE_FLOAT_MODEL.md). 001026D0, 00102900 and 00102738
  return the model's status, `EM_EE_FLOAT_OK` or the nonzero refusal. A caller turns a
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
- em_crate_original's `em_crate_sdk_multiply` (the crates' and drums'
  001026D0), since the crate oracle runs on the measured model.

00102900:
- em_shadow_actor_route (001F8D30's colour)
- em_actor_light_001D89D0 (001D8340's fold)
- em_camera_leftovers (`cl_v_scale4`)
- em_snow (001E67C0's tile colour, 1.3 x strength)

00102738:
- em_coll_probe_original (`em_coll_probe_sdk_dot`, also em_actor_collision's)
- em_coll_list_passes_walkers (`sdk_dot`), em_coll_grid_hull (`vdot`),
  em_coll_move_original (`vdot`)
- em_pickup_items_original (`vdot`, the aura's facing)
- em_owner_draw_original (`inner_00102738`, 001CA7B0's cull planes)
- em_actor_light_001D89D0 (001D8340's squared distance)
- em_camera_follow_original (`v_dot`)
- em_camera_area11_specials (`dot3`)
- em_camera_leftovers (`cl_v_dot`: em_camera_leftovers_solver and
  em_camera_commit_original)
- em_crate_original's `em_crate_sdk_dot3` (the drums' player distance)

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

- **001026A0, 00103230, 001028B8, 001028D0 and 00102850** have several
  copies, for example em_effect_original's `sdk_apply` (exported as
  `em_effect_original_001026A0`, which the crate and em_camera_rotation now
  call), em_status_models' `vu_row`, em_locomotion_display's
  `em_loco_00103230`, em_coll_list_passes_walkers' `sdk_scale`, and the
  per-lane host-double helpers listed in EE_FLOAT_MODEL.md section 5c. They
  are candidates for this header.

## Evidence

- `tools/test_sdk_vu0_reference.py` (`make test-sdk-vu0-reference`) executes
  the ORIGINAL 001026D0, 00102900, 00102948 and 00102738 from the user's ELF.
  It covers every special value in every lane, the three aliasing forms of
  001026D0, the aliasing form of 00102900 and 00102948, 00102738 with a = b
  and a separate b (its f0 result, and that it stores nothing), and a random
  sweep: 300 cases quick, 3,000 full. It compares the whole guarded window, so the destination and
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
  - for 00102738 and the crate: test_coll_probe_reference,
    test_coll_list_passes_reference, test_coll_grid_hull_reference,
    test_coll_move_reference, test_pickup_items_reference,
    test_owner_draw_reference,
    test_actor_collision_reference, test_camera_follow_original_reference,
    test_camera_area11_specials_reference, test_crate_original_reference,
    test_drum_original_reference, test_head_sprite_reference
  - the level smoke
