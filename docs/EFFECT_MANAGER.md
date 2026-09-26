# Effect manager barrel 001F0360, the lane draw 001F0720 and the pickup glint 001F0A60

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "effect-manager" (census lane L26-effect-manager), 2026-09-23. The
translation and its oracle are done. **Live since 2026-09-25** (the effects
step, census L26 / L27 / L39): `src/game/em_effects_live.{h,c}` binds this
module with em_effect_original, em_effect_kinds, em_head_sprite_original and
em_player_equipment_sprite over the one render context; section 8 is the
live binding, section 5 the brief it followed.

Files:
- `src/game/em_effect_manager.{h,c}`: the native translation.
- `tools/test_effect_manager_reference.py`: the original-instruction oracle,
  including the capture checks (`make test-effect-manager-reference`, see
  section 6 for the Makefile hunk).
- `tests/effect_manager_test.c`: the native contract test (ASan/UBSan, no disc
  data).

## 1. Census rows of this lane: before and after

| Function | Decomp | Census before | After this lane | Module / evidence |
|---|---|---|---|---|
| 001F0360 barrel | BM | missing (UM_001F0360) | live (section 8) | em_effect_manager; oracle on 15 beats + capture |
| 001F6210 area model sprites | BM | missing | live (section 8) | em_effect_manager; oracle (AREA11 path + every list) |
| 001F6BB0 area 0 / 0x1301 selector | NM | missing | live (section 8) | em_effect_manager; oracle, from the .s |
| 001F6EB0 area 7 / 0x12 track switch | BM | missing | live (section 8) | em_effect_manager; oracle |
| 001F40C0 entity sweep | BM | missing | live (section 8) | em_effect_manager; oracle |
| 001F0720 ring-lane age + draw | NM | missing | live (section 8) | em_effect_manager; oracle, from the .s; capture: 24 packets per beat |
| 001F0A60 glint sprite | AU | missing | live (section 8) | em_effect_manager; oracle, from the .s; capture: beats 03/05/13 |
| 001F4D40 pulsed 001CD520 sprite | AI | missing | live (section 8) | em_effect_manager; oracle, from the .s |
| 001F1110 aura init | BM | missing | **live** (classification correction) | em_pickup_items_original, bound in em_area11_interaction_host.c; test_pickup_items_reference executes it |
| 001F1180 aura step | NM | missing | **live**, its draw block too (section 8) | em_pickup_items_original (live); the draw block 0x1F136C..0x1F1470 is `em_effect_manager_aura_draw` through em_effects_live (the interaction host's aura-draw hook) |
| 001EA240, 001EF940, 001EF9D0, 001EFD20, 001EFD90 | BM/NM | verified-unbound | live (section 8) | em_effect_original (docs/EFFECT_ORIGINAL.md); binding notes in section 5.4 |

No census row of this lane is left "missing" or "unverified".

## 2. What the original does (all read from the original code)

Addresses in brackets are the instructions a rule comes from.

### 001F0360: the barrel

Called once per frame by the world-frame variants 001AE5E0 and 001AE6B0.
It calls, in order: 001F6210, 001F5C20, 001F6BB0, 001F6EB0, 001F40C0, then
001F0720 with 0, 1, 3, 4, 5 and 6. Lane 2 is never drawn by it. That lane holds
two live slots in every route snapshot, and they never age.

**What it does in AREA11** (area 0x0B, room 0 in every route snapshot):
- 001F6210: 001F5CA0 returns 0 for key 0xB00, so it returns.
- 001F5C20 (lane L27): the list of 001F5640 for key 0xB00, with 001F5940 → 001F4D40 per record.
- 001F6BB0 and 001F6EB0 return without a call (key 0xB00).
- 001F40C0: no D_007709C0 entity is live on the route, so it only runs D_00275C44 -= 1.
- 001F0720 ×6: ages the six lanes and emits their four packets each (below).

### 001F0720(n): ring-lane age and draw

- n outside 0..6 returns at once (0x1F074C).
- The presets come from jtbl_0026E9E0:

  | n | count | scale | colour record |
  |---|---|---|---|
  | 0 | 1 | 64 | D_00259CD0 + 0 |
  | 1, 2 | 2 | 64 | D_00259CD0 + 0x40 |
  | 3 | 2 | 64 | D_00259CD0 + 0x80 |
  | 4 | 2 | 80 | D_00259CD0 + 0x80 |
  | 5, 6 | 1 | 64 | D_00259CD0 + 0xC0 |

- **Age pass** over the 32 slots of lane n. The lane is D_0028F700 + 0x4DBEC0 + n·0xC00, the same storage as the 001F0460 ring in em_effect_original.
  - total = count·60.
  - A countdown +0x58 > 0 is decremented and reloaded. When it is now below total, f = cvt(t) / cvt(total) (EE div). f goes to 0x70003A20, and +0x4C = scale · f.
  - A countdown ≤ 0 is stored as 0.
- **Four packets** on chain D_007635C0, id 0 (001CB5F0):
  1. 1 qw: 0x11000000, 0x14000000, 0x11000000, 0.
  2. 0xC1 qw: tag 0x01000101 / 0x6CC00020, then the whole 0xC00-byte lane.
  3. 5 qw: tag 0x6C04000E, then the preset's four colour quadwords.
  4. 0xA qw: tag 0x6C090000, then 001CD370(2) (context +0x22C0, 4 qw), the scratchpad 0x70003AC0 matrix (4 qw), and the context +0xA0 quadword.
- Then 001CB760(chain, 0, D_00233290) and 001CB900(chain, 0, 1).

### 001F0A60(a0, mode, pos, colA, colB, angle, size, depth): the glint

This is two camera-facing triangles around one point.
1. v = 001CD370(0) · (pos.xyz, 1). A VCLIPw.xyz flag in the latest judgement returns with nothing done [0x1F0AF4].
2. 0021B9A0(2, 1.0, 150.0), then 0021B9A0(3, 1.0, 150.0). This is the fog/depth-range programmer (section 4): near += 150, then far += 150.
3. Only now is the context +0xA0 fog quadword read.
4. v = K · (pos.xyz, 1) with K = 0x70003AC0. The unscaled v is kept, w₀ = v.w.
   - xy ·= 1/w.
   - w′ = w − depth (the w lane is overwritten).
   - z ·= 1/w′.
   - w = clamp(fog.z + fog.w·w′, 0, fog.x).
   - All four lanes go to 12.4 fixed point at 0x70003600.
5. 0x70003400 = (size, 0, 0, 1) and 0x70003410 = (0, size, 0, 1). 0x70003440 = the identity rotated about z by `angle` (001029C0, 00102A60). Both corners are rotated by it (001026A0).
6. For each corner c, the offset is o = ftoi4(xy / w) of S′·(c.x/2, c.y/2, w₀, 1). S′ is the 0x70003A40 matrix with **row 2 x and y cleared**: the original subtracts row 2 from itself in x/y [0x1F0CA4, 0x1F0D10].
7. With mode ≠ 0, colA and then colB pass through the fog weight f = (0x7000360C >> 4) clamped to 0..255:
   - mode 1: alpha = alpha·f >> 8;
   - modes 2, 3, 4: r, g, b = c·f >> 8, and 0x7000360C = 0xFF0. So colB is then weighted by 255 (the original re-reads the word);
   - other modes: unchanged.
8. The packet opens 0xE qw on chain D_007635C0 + (a0 << 15), with id = the 0x70003608 word (the depth key):
   - VIF 0x5000000D;
   - GIF tag 0x6035400000008002, regs 0x414141;
   - twice (corner 0, corner 1): colB at centre + o (its fog word | 0x8000), colA at the centre, colB at centre − o.
9. Then 001CB900(chain, depth key, mode) and 0021B9A0(1, 0, 0), which restores preset pair 1.

### The draw block of 001F1180 (the map pickup's aura, 0x1F136C..0x1F1470)

001F1180's countdown, record choice, facing test and ramps are em_pickup_items_original. Its NEARMISS C has the facing test inverted, and that module follows the .s. The draw block itself:
- s = 0011E2A8(π·timer) and size = rec+0x24 · s.
- colA = rec+0 | rec+4 << 8 | rec+8 << 16 | float_to_int(128·s) << 24.
- colB = rec+0xC | rec+0x10 << 8 | rec+0x14 << 16 | float_to_int(16·s) << 24.
- p = 001026A0(owner +0xD0, (rec+0x18, rec+0x1C, rec+0x20, 1)).
- Then 001F0A60(0, 1, p, colA, colB, (π·angle)/180, size, rec+0x28).

The records are D_00259DD0 + variant·0x2C. For variant 1 the table is picked by area: D_0025A040 (area 2), D_00259F90 (area 1), else D_00259EE0. The AREA11 map owner at 0x7A67E0 is variant 1, so its records come from D_00259EE0.

### 001F6210: area model sprites

For each 0x28-byte record of the 001F5CA0 list (a halfword +0 ≥ 0 continues), bracketed by 001D8C20(1)…001D8C20(0):
1. owner = the context +0x1C cursor.
2. handle = 001C6120(D_0028A59C, rec+4).
3. M = identity, then 00102C58 by the angles rec+0x18..0x20, then 00102918 by the position rec+0xC..0x14. The decomp header calls these "translate" and "scale", which is wrong.
4. The colour is row 0 of D_0026EB20, or the row of the first of the 17 D_0025CA40 keys equal to rec+4 + (((area<<8)+room)<<8).
5. The jitter, with a = colour.w:
   - t = −a + (a − (−a))·(2⁻³¹·rand);
   - rgb += rgb·t;
   - colour.w = 0.
6. Display-list block 5:
   - header +3 = 0x10, +4 = 0, +0 = 5; cursor += 0x60;
   - 0x70003400..0x7000346F are cleared;
   - 0x70003470 = colour + D_0026EB60 (001028B8);
   - tag 0x11000000 / 0x01000101 / 0 / 0x6C0403F5 + the 4 qw at 0x70003440.
7. Block 9 (cursor += 0xA0), tag 0 / 0x01000101 / 0 / 0x6C080000, then:
   - 001026D0(0x70003AC0, M);
   - 001026D0(0x70003400, M), whose matrix is all zero.
8. 001D3D90(handle). It may advance the cursor, and the original re-reads it.
9. Block 0 (+3 = 0x60, cursor += 0x10).
10. 001CAAC0(&(rec+0xC.., 1.0), owner, context).

In AREA11, 001F5CA0 returns 0. The lists exist for keys 0x301, 0x302, 0x400, 0x401, 0x700, 0x800, 0x803, 0xD00, 0xF00 and 0x1500.

### 001F6BB0 and 001F6EB0: area selectors

- **001F6BB0** returns for keys 0x1100, 0xE00, 0x200, 0x100, 2 and 1.
- **Key 0x1301, sub-mode in {2, 4, 5, 8}:** 001F6850 when record D_0025D270 or D_0025D2C0 is ready (001F6AC0: its word +0x24 ≠ −1).
- **Key 0x1301, other sub-modes:** 001F6640(D_0025D270) when that record is not ready and D_00810778 and D_0081077B are both not 0xFF. Then 001F6640(D_0025D2C0) when that record is not ready and D_0081079E == 0xFF. The decomp comment claims the first guard needs both bytes equal to 0xFF; the .s and the C need both to differ from 0xFF.
- **Key 0 with D_0081075D == 0xFF:** p = 001F6760(), then 001F66F0(p) when p is ready. The records are the room point-light list (critic note 7.2). This path did **not** run on the route: 001F6BB0 and 001F6EB0 first ran in S2 (area 0x0B), and 001F6AC0 (armed) never ran. The S1 hits of 001F6760/001F66F0/001F6640/001F6850/001F6E40 come from their other callers, 001F68B0 and 001D7BB0.
- **001F6EB0:** keys 0x700 and 0x1200 read D_0025D524 or D_0025D6E4.
  - −1: 001F6E40(D_00810702, area<<8) unless the sub-mode is 1.
  - Otherwise: 001F6E80(…) when the sub-mode is 1.

### 001F40C0: the D_007709C0 entity sweep

For i = 0..0x7F with +0x80 == 0:
1. 001F3620(e, +0x82).
2. If still live: 001F3E30(0x700036A0, e+0x40, rec+0x50, rec+0x54+4·(i%2), rec+0x5C) with rec = D_0025A350 + (+0x82, re-read)·0x60.

Then D_00275C44 −= 1. 001F3FA0 (lane L27, S0) clears the entities and zeroes the counter. 001F3620 reads it.

### 001F4D40(pos, colour, f12, f13)

- k = (rand >> 23) & 0xFF.
- p = (3·a + (a·k >> 8)) >> 2 with a = colour +0xC, using 32-bit products and logical shifts.
- rgb = (b·p >> 7) << 16 | (g·p >> 7) << 8 | (r·p >> 7).
- Then 001CD520(0, 2, pos, 0x20045B0599421EF0, rgb, f12, f12, f13).

On the route it is reached through 001F5C20 → 001F5940 (lane L27).

## 3. The translation

- Every function above is a C function in `em_effect_manager.c`, named `em_effect_manager_<address>`. The draw block is `em_effect_manager_aura_draw`, the `w_draw` worker of `em_pickup_aura_001F1180`.
- Floats cross the API as raw bits. COP1 and VU0 arithmetic goes through `em_ee_float.h` under each step's own form.
- **Reused verified translations:**
  - em_effect_original: 001026A0, float_to_int 001281C0, and the 001F0460 ring type `EmEffectOriginalDecals` (001F0720 ages that same storage);
  - em_owner_services_original: 001029C0, 00102A60, 00102C58, 00102918.
- **Inline leaves:** 001F6AC0, 00102948, 001028B8 (VADD), and 001026D0 (four 001026A0 rows).
- **Static ELF data** (identical in the ELF and in all 15 route snapshots; asserted) is loaded by `em_effect_manager_load_tables`: 0x259CD0 (+0x100), 0x259DD0 (+0x320), 0x25A350 (+0xA20), 0x25CA40 (+0x4B0) and 0x26EB20 (+0x50). A read outside these windows faults BAD_INDEX; the original would read on.
- **Fail-stop:**
  - a reached NULL worker or view, or a negative worker result, latches the fault, and later calls return −1;
  - the barrel checks everything it reaches on every call before its first write or call. Workers of the conditional paths (001F6210's list loop, the selectors, the entity sweep) are checked where the path is decided.
- **Faults that cannot be reached** (kept as guards):
  - a non-finite VCLIP input: the VMULAx/VMADDw forms clamp, so the clip input is always finite;
  - a fog weight ≥ 256: fog.x is always 255.0, because 0021B920 writes it and 001F0A60 calls 0021B9A0 → 0021B920 before reading the fog. Swapping the ≥ 256 clamp value therefore cannot be observed; it is the only mutation that survived (section 6).

## 4. Corrections found on the way (for the lead; no existing file was edited)

1. **0021B9A0 is the fog/depth-range programmer, not a pad rumble.** Its NEARMISS C:
   - mode 1 = preset pair +0xD8/+0xDC;
   - modes 2/4: near = bias + near·scale;
   - modes 3/5: far = bias + far·scale;
   - modes 0/other: preset pair +0xF8/+0xFC;
   - then 0021B920 writes context +0xA0 = (255, 2048, far·k, −k) with k = 255/(far − near).

   `em_effect_original.h` ("pad rumble"), docs/EFFECT_ORIGINAL.md (steps 1 and 6 of 001EA240 and the Boundaries table) and SCENE_COORDINATOR_DESIGN.md ("0021B9A0 rumble channel") carried the wrong label (all three corrected in the Effects step, 2026-09-24). 001EA240's calls (2, 1, 100), (3, 1, 100) … (1, 0, 0) push the fog range out while a puff draws. Its worker must be bound to the same fog programmer as here.
2. **Census row 0021B9A0** says "verified-unbound (em_game, em_effect_original)". test_effect_original_reference only stubs it, and no native translation exists. It is missing (lane L31).
3. **Census rows 001F1110 and 001F1180** say "missing". Both are translated and live (above). The census missed em_pickup_items_original.
4. **001CB5F0 / 001CB760 / 001CB900** (census: verified-unbound via em_head_sprite_original) have no native translation. They are workers there and here, so the packet sink still has to be written.
5. **Decomp comments:**
   - 001F6BB0's header says the slot-0 guards must be 0xFF; they must not be.
   - 001F6210's header says "translate … scale"; the calls are rotate (00102C58) and translate (00102918).

## 5. Binding (the brief; live since 2026-09-25, section 8)

### 5.0 Prerequisite: the render-context views (found by the Effects step, 2026-09-24)

**Met since the render context step (2026-09-25, docs/RENDER_CONTEXT.md
section 8).** The one canonical render context runs live: every world frame
head builds P / V / K and the four 001D2D20 projections (+0x2240..+0x233F)
from the camera pool's D_00810610, copies P to 0x70003A40 and K to
0x70003AC0, and re-programs the fog block +0xA0 with 0021B9A0(0, 0, 0); the
area load writes the fog presets (001D8FD0); main-loop step B sets the packet
cursors (+0x18 among them) and 001D1EA0's 001CB800 splices and clears the
chain table D_007635C0 each frame. The effect binding reaches all of them
through em_rcl_bytes / the module's views (D_00810E80 is em_frame's). What
follows is the finding as it was written.

The Effects step tried to bind this module, em_effect_original, em_effect_kinds,
em_head_sprite_original and em_player_equipment_sprite live, and stopped here.
Every draw of theirs reads original render-context bytes that no live code
produces:

| Reader | What it reads |
|---|---|
| 001CCF70 (the driver 001EA240's depth key; the head sprite's ramp tick) | context +0x2240 (001CD370(0)), scratchpad 0x70003AC0, context +0xA0 |
| 001CFB50 / 001D0540 (every puff handler) | scratchpad 0x70003AC0, D_00275670 |
| 001CFBE0 (every puff handler, the head sprite) | scratchpad 0x70003A40, context +0x2240, 0x70003AC0, +0xA0; the packet cursor +0x18, D_00810E80, the chain table D_007635C0 and the packet buffers |
| 001F0720 (the barrel's six lanes) | context +0x22C0 (001CD370(2)), 0x70003AC0, +0xA0; the chain |
| 001F0A60 (the pickup glint), 001F4D40 → 001CD520 (the glow markers) | context +0x2240, 0x70003AC0, 0x70003A40, +0xA0 (001F0A60 also calls the fog programmer 0021B9A0 on that block) |

The node lifecycle itself does not depend on them, but 001EA240 calls
001CCF70 and the handler on every state-1 tick. A spawn bound without the
views would fault at the first footstep puff (fail-stop) and end the level,
which is worse than today's counted gap (em_player.c `player_effect_gap`).
So nothing here is wired yet.

In the original the views come from the frame head 001D1C50:
- 001D2960(D_00810610) writes P (+0x2340), V (+0x2380), K (+0x23C0) and the
  four 001D2D20 projections (+0x2240, +0x2280, +0x22C0, +0x2300);
- 001D1C50 copies P to 0x70003A40 and K to 0x70003AC0;
- the fog block +0xA0 comes from 0021B970 at the area load and from
  0021B9A0(0, 0, 0) per frame (while bit 0x80 is clear).

The binding therefore needs, first (lanes L32 and L30):
1. **One canonical render-context block** (FRAME_RENDER_HEADS.md section 4:
   "The port has no canonical render-context storage today"), with a packet
   window and the chain table for em_packet_chain_original.
2. **em_frh_001D2960 bound over the live view.** D_00810610 is the live
   camera's view in the original convention. em_snow_runtime already derives
   that form from the native view (rows 1 and 2 negated). Its
   em_snow_projection_matrices was a private copy (deleted since) of 001D2960's P / K and of
   the +0x2240 projection. It must become a reader of the one owner, so that
   one original keeps one translation.
3. **The fog block written by the fog programmer**
   (em_packet_chain_0021B9A0 / 0021B920, which the Metal fog already uses
   since this step; docs/PACKET_CHAIN.md).

Then the effect binding below applies as written. The puffs' pixels still
need the renderer to consume their 001CFBE0 packets. Their VU1 program is
table 0x231770 (kind 1), the program the live AREA11 flame already draws
through em_effect_sprite_project over em_snow_particles_generate
(docs/AREA11_EFFECT.md), fed by the source block (D_002568B0 and the
others) and the transform block that em_effect_kinds_001CFB50 now fills.

### 5.1 The barrel

- **Live call site:** `em_scene_bindings.c` `w_001F0360`: `em_effects_live_001F0360()` in the roster scene, −1 mapped to the scene fault (section 8.2); `unmirrored(UM_001F0360)` remains only for a scene without the roster.
- **The manager holds:**
  - `tables` from `em_effect_manager_load_tables(elf)`, the same ELF buffer the other original modules read;
  - `globals` mirrored per frame from em_scene_state: D_00810700/01/02, and the bytes D_0081075D/778/77B/79E. D_00275C44 is owned by this module and starts at 0 after 001F3FA0 (L27). The scratchpad words 0x70003400..7F, 3600..0F and 3A20 are module-local, since no other translated reader exists;
  - `decals`: **the same `EmEffectOriginalDecals` as em_effect_original** (001F0460 writes it, 001F0720 ages and draws it). Its initial bytes must be 001F03D0's reset (L27);
  - `view`: context +0x2240 (001CD370(0)), +0x22C0 (001CD370(2)), +0xA0 (fog), and the scratchpad matrices 0x70003AC0 and 0x70003A40, as the original holds them when the barrel runs. The lane-draw capture check shows the latest frame's packet 4 equals the snapshot's view;
  - `entities`: D_007709C0 +0x80/+0x82. No entity is live on the route, and the writer 001F4010 never ran.
- **Workers:**

  | Worker | Needed in AREA11 | Bind to |
  |---|---|---|
  | w_001F5CA0 | yes (returns 0) | lane L27's 001F5CA0 translation (a pure key switch) |
  | w_001F5C20 | yes | lane L27's 001F5C20 (which reaches 001F5640, 001F5940 and **em_effect_manager_001F4D40**) |
  | w_001CB5F0, w_001CB760, w_001CB900 | yes | the renderer's packet sink (not yet written; section 4.4) |
  | 001F6210 loop workers, w_list_at | no | NULL until another area is in scope (a NULL reached worker faults) |
  | 001F6BB0/6EB0 workers, w_word | no (see 5.3) | NULL |
  | w_001F3620, w_001F3E30 | no | NULL |

- **Prerequisites:** the barrel should stay unwired until L27 (001F5C20 and the 001F03D0 ring reset), the packet sink and the render-context view exist. Otherwise it would fault every frame by design.

### 5.2 The pickup glint

- **Live stand-in:** `aura_draw` in `em_area11_interaction_host.c`. It is a no-op that prints "not translated; not drawn" once, so the map pickup's glint is missing today.
- **Replacement:** an adapter that calls
  `em_effect_manager_aura_draw(&manager, owner_d0, record, angle, timer)`, where `owner_d0` is the stepping owner's `slot->world` (its +0xD0 matrix, raw bits).
- **Workers:**
  - `w_0011E2A8` → `em_sdk_math_original_w_0011E2A8` (bits ↔ float adapter);
  - `w_0021B9A0` → the fog programmer (L31; it must rewrite `view.fog` as 0021B920 does);
  - `w_001CB5F0` and `w_001CB900` → the packet sink;
  - `view` as in 5.1.
- **Frame order:** it runs inside the owner tick (0015AE20's tail while D_70003B92 == 0), where em_pickup_aura_001F1180 already runs.

### 5.3 001F6BB0 / 001F6EB0 call paths (not on the route)

Neither selector takes a call path on the route. They first run in S2, where the key is 0xB00, and 001F6AC0 never ran in the census. Their workers (L27's point-light chain 001F6760/001F66F0/001F6640/001F6850, and 001F6E40/001F6E80) stay NULL in AREA11. The S1 point-light calls the census saw come from 001F68B0 and 001D7BB0, whose stand-in is the offline export (tools/export_point_lights.py, critic note 7.2), not from these selectors.

### 5.4 em_effect_original (001EA240, 001EF940, 001EF9D0, 001EFD20, 001EFD90)

These are unchanged. Bind them as docs/EFFECT_ORIGINAL.md "Binding" says, with one correction: `w_0021B9A0` is the fog programmer (section 4.1), not a rumble service. The per-subtype handlers and 001CFB50/001CFBE0 belong to lane L27/L39. The ring they write through 001F0460 is the `decals` this module draws.

## 6. Verification

`python3 tools/test_effect_manager_reference.py`: quick ≈ 3.3 s (3 of 15 beats); `EM_TEST_FULL=1` ≈ 56 s (all 15).

- **The oracle** executes the original instructions of every function in section 2 and of the leaves 001CD370, copy_qw4, 00102948, 001029C0, 00102A60/001029E8, 001026A0, 001026D0, 001028B8, 00102C58, 00102918, float_to_int, 001F5CA0 and 001F6AC0. Memory is each route snapshot. The interpreter is the one in test_effect_original_reference.py, extended with the integer multiply, divide, HI/LO moves and bitwise NOR it lacked, and a second stack.
- **Workers bound to original code on both sides:**
  - 0021B9A0 (with 0021B920/0021B900) and 0011E2A8 are run as original code. The oracle runs them in place; the native worker runs them in a shadow copy of the RAM, and the fog state (context +0x80..+0xFF) must end equal;
  - the native side of 001F5CA0 runs the original on the shadow.
  - The rest are recorded with scripted results.
- **After every call, these must hold:**
  - every byte the original changed is modelled (or stack/scratch);
  - globals, scratchpad words, ring bytes, entities, the display list and fog are equal;
  - every packet byte (prefilled 0xA5 on both sides) and every worker call with argument bits is equal.

| Part | quick | full |
|---|---|---|
| barrel frames (captured frame, then lockstep with synthetic live slots on every age boundary) | 12 | 615 |
| 001F0720 synthetic (n = 0..7, −1, INT_MAX, INT_MIN; countdowns on every boundary) | 33 | 660 |
| 001F0A60 direct (visible / clipped, modes 0..5, −1, 0x100, depth ±5000, both chains) | 72 (54 drew) | 3,000 (979 drew) |
| 001F1180 whole: captured, flipped eye, forced state (em_pickup_aura_001F1180 + aura_draw) | 78 ticks (28 drew) | 4,800 (365 drew) |
| 001F6210: captured AREA11 + all 10 lists + a colour-window case | 12 calls, 17 records | 180, 255 |
| 001F6BB0 / 001F6EB0 cases | 70 / 70 | 5,385 / 5,385 |
| 001F40C0 sweeps (live entities) | 3 (119) | 300 (11,520) |
| 001F4D40 | 40 | 4,500 |
| fail-stop (NULL workers: fault, nothing written, no call) | 5 | 75 |

**Capture evidence (independent of the model):**
- **Lane draw.** Each snapshot's two DMA buffers hold the original's 001F0720 packets for the last two frames: six lanes 0xDD0 apart.
  - In all 15 beats the latest buffer holds **all 24 native packets byte for byte**, including packet 4's matrices and fog (360 packets).
  - The previous frame's buffer matches packets 1..3 in every beat (270). Its packet 4 matches in 7 beats, where the view was static (42).
- **Glint.** Beats 03, 05 and 13 hold the map pickup's glint packet.
  - There is exactly one aura state that ramps to the captured one: the angle in [−180, 180] and the unique EE pre-image of the timer.
  - With the snapshot's view and the original 0021B9A0 on the snapshot's context, it reproduces the latest frame's 224-byte packet in all three beats.

**Mutation check.** 50 single-point bugs were injected into em_effect_manager.c, and the quick run was repeated after each. They covered:
- preset scale and count, the age compare and divide, the ring field, the lane tag;
- the fog store and clamp, the alpha shift, the vertex fog bit, the depth subtract, the corner half, the row-2 clear, the 0021B9A0 modes, the colour swap, the angle divide, the size multiply, the fog read order, the kept w, the clip flags and mask;
- the chain shift and the min lane;
- the 001F4D40 shift, constant and product;
- the kind re-read and parity, the counter step;
- the selector guards, the key and block headers, the cursor steps and owner, the jitter lanes and colour w, and the second 001026D0 matrix.

49 fail the test. The survivor is the fog-weight ≥ 256 clamp, which cannot be observed (section 3).

`tests/effect_manager_test.c` covers: one barrel frame's worker protocol, one 001F0720 age step and its four packet headers, out-of-range presets, NULL workers and views (fault before any write), latched faults, WORKER_FAILED, BAD_INDEX for an entity kind past D_0025AD70 and a record past the aura window, and the clamped clip transform.

**Makefile hunk (test targets; the lead applies it):**

```make
.PHONY: test-effect-manager
test-effect-manager:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/effect_manager_test.c src/game/em_effect_manager.c src/game/em_effect_original.c src/game/em_owner_services_original.c -lm -o build/effect_manager_test && ./build/effect_manager_test

.PHONY: test-effect-manager-reference
test-effect-manager-reference:
	python3 tools/test_effect_manager_reference.py
```

When bound, add `src/game/em_effect_manager.c src/game/em_effect_original.c` to COMMON. em_owner_services_original.c and em_pickup_items_original.c are already there. The app built with them added gives zero warnings.

## 7. Limits

- **VCLIPw is not part of EE_FLOAT_MODEL.** Both sides compare DAZ'd finite values; a non-finite input cannot reach it (section 3).
- **Unexercised on the route**, verified only on synthetic inputs over captured RAM:
  - 001F6210's list loop (other areas);
  - 001F6BB0/001F6EB0's call paths;
  - 001F40C0's live-entity path;
  - 001F0720's float path (every AREA11 lane the barrel draws is empty in the snapshots);
  - 001F4D40.
- **Per-case coverage of the route** is not measured (census 7.1): the captures show the barrel's AREA11 path and the glint's mode-1 path.
- **Not translated here:**
  - 001F5C20, 001F5CA0, 001F3620, 001F3E30, 001F6640/66F0/6760/6850, 001F6E40/6E80 (lane L27 or other areas);
  - 001C6120, 001D3D90, 001CAAC0, 001D8C20 (anim/render lanes);
  - 001CD520 (L28);
  - 0021B9A0 and the packet sink 001CB5F0/001CB760/001CB900 are translated
    since afa091b (em_packet_chain_original, docs/PACKET_CHAIN.md), and
    001CFB50/001D0540 since the Effects step (em_effect_kinds).

## 8. The live binding (census L26 / L27 / L39, 2026-09-25)

### 8.1 One binder: em_effects_live

`src/game/em_effects_live.{h,c}` adds no behaviour: it owns the storage the
effect translations share and wires each one's workers to the other
translations over the canonical render context (em_render_context_live,
RENDER_CONTEXT.md section 8).

| Storage (original) | Owner here |
|---|---|
| the effect entity tables, D_00255430, this module's windows, the lists 0x25AD80.., the handlers' sources D_002565E0.., D_002535F0 / D_00253670 / D_00251260 | `assets/effect_tables.emet` (`tools/export_effect_tables.py`: ten ELF windows, each checked equal in the opening, playable and route 00..14 captures), placed in an ELF-sized image the translations' own `*_load_tables` read |
| the effect nodes (001AFA90 records) | the actor pool; the bytes beyond the pool's fields (+0xD0 matrix, +0x1F0 work block, the head sprite's +0x24.. fields) in one slot per pool record, synced into the pool record after every call |
| the ring D_0028F700 + 0x4DBEC0 and D_0081F950 | one `EmEffectOriginalDecals` (001F0460, 001F0720, 001F03D0) |
| D_007709C0 (0x80 particle records), D_00275C40 / D_00275C44 | em_effect_kinds' particles; 001F40C0's entity view and D_00275C44 are synced around the barrel (one word) |
| D_0081F8F0, 0x700036A0.., 0x70003660.., 0x70003600.. | the binder (one copy each; 001CCF70's 0x70003600 / D_00275C04 are em_effect_original's for the puffs and the head sprites alike) |
| the views (+0x2240, +0x22C0, +0xA0, 0x70003AC0, 0x70003A40, the +0x18 cursor, D_00810E80) | read from the render context at every entry; the fog copies after every 0021B9A0 |

| Worker (original) | Bound to |
|---|---|
| 001AFA90 / 001AFC10 | em_actor_pool (the node is bound to its pool behaviour by its +0x10 after 001EF9D0 wrote it: em_area11_bindings `bind_spawned`) |
| 00122BB8 | em_random_next (the one game rand) |
| 0021B9A0 | em_rcl_0021B9A0 on the render context |
| D_00255434[subtype] | em_effect_kinds_handler (001EC1F0, 001EC3F0, 001EC470, 001EBF10); of the handlers it does not translate, the two packet-only ones (001EAD70, 001EC270) are the counted gap and every other faults (8.2) |
| 001CFB50 / 001CFBE0 | em_effect_kinds_001CFB50 / em_head_sprite_original_001CFBE0 (the cursor re-read at every call) |
| 001CCF70, 001CD370, 001CFA60 | em_effect_original, the context's +0x2240 |
| 001CB5F0 / 001CB6B0 / 001CB760 / 001CB900 | em_packet_chain_original's adapters on the render context's chain (em_rcl_packet_chain) |
| 001F5C20 / 001F5CA0 / 001F5940 | em_effect_kinds; its 001F4D40 is this module's, whose 001CD520 is em_player_equipment_001CD520 (the marker position travels as the binder's handle for the stack quadword 001F5C20 builds) |
| 0011E2A8, 0011DF78, 001281C0 | em_sdk_math_original (the collision world's SDK context), em_effect_original_float_to_int |
| 001D7FA0 | em_point_light_register, the result dropped (EFFECT_ORIGINAL.md "Full point-light pool"; no route record has a light) |
| 001FBF50 / 001FB9F0, 001F6210's list workers, the selectors' point-light workers, 001F3620 / 001F3E30 | NULL: a fault if reached (no first-level effect record carries a sound; AREA11's key has no model-sprite list; keys 0 / 0x1301 read latch bytes the port does not keep canonical and fault before the call) |

### 8.2 Where each runs

| Original | Live position |
|---|---|
| 001F0360 | w_001F0360 in both world variants (em_scene_bindings), after the walk, as 001AE5E0 / 001AE6B0 call it; the reported no-effect binding remains for a scene without the roster |
| 001F0310 | w_001AFCA0's 001D0660, after the pool reset (the effect and equipment binders attach there); 001D1C10 (the movie frame) does not run |
| 001EFD90 | the player closure's workers (the footsteps 00187EE0, the climb's 0017DEB0, the slide, the walk's skid 001612D0, the fall and reaction spawns), with the record's whole +0xC0 quadword; the crates' 001551B0 |
| 001EFD20 | the truck 00823FF0's 32 spawns, the drums' 00156620, the weather node 001C1EA0 (0x80000017) |
| 001F0120 | 0015C420 (the player's 0x3B), Roger's 001BA8E0 (0x47) |
| 001EA240, 001E2560 | the pool walk 001AFD70, rows 0x1EA240 and 0x1E2560 of em_area11_bindings |
| the draw block of 001F1180 (001F0A60) | the pickup owner's tick (em_area11_interaction_host `aura_draw`, the owner's +0xD0 matrix, through the hook `em_area11_interaction_host_set_aura_draw` the bindings' effect attach sets; unset it faults) |
| 001E67C0's fog | em_snow_runtime's tick: 0021B9A0(2, 0, 0), (3, 0, 300) before the emission (its fog quadword is the one the draw uses), (1, 0, 0) after |
| 001D04B0's fog (the AREA11 effect owner) | its DRAW call copies the context's +0xA0 at the owner's walk position (001CFBE0 copies it; the owner programs no fog) |

**Untranslated handlers.** 001EA240 calls D_00255434[subtype]; the
handlers em_effect_kinds does not translate split in two:

- **Packet-only: the counted gap** (`effects_gap` in em_effects_live.c:
  counted in the binder's counters, reported once per handler, returns 0;
  the level smoke asserts the route counts none). Only the skid's two:
  001EAD70 (0x80000033, subtype 1; decomp C byte-matched) and 001EC270
  (0x80000012, subtype 0xB; its split listing). Each stores only the work
  block's +0x1F4 (D_00275C34 + 4, the LCG, twice), which 001EA240 rewrites
  from +0x1F0 before every handler call and reads nowhere else, and calls
  only 001CFB50 (it rewrites D_0081F8F0 +0x00..+0x57 in full; in the port
  that block's only reader is the 001CFBE0 each translated handler calls
  right after its own 001CFB50) and 001CFBE0 (the packets). Skipping one
  therefore loses only its packets; the node's ageing and free are exact.
  The player reaches both off the route: the reversal skid (001612D0)
  spawns 0x80000033 on surfaces 5 / 6 and 0x80000012 elsewhere (with +23C
  and +23D clear), and the slide (0016CD70) spawns 0x80000012 on every
  surface outside its list (5, 6, 7, 8, 0x5A..0x5C). A
  scratch run reversing the stick every 45 frames for 900 frames after
  first control counted both and played on (no fault).
- **Everything else faults** (fail-stop at the handler's address, latched
  by the binder; the scene coordinator stops the game task). None of them
  is checked, and some do more than draw: 001EF510 (subtype 6) spawns
  001EFD90(0x80000036) from inside the handler when the work block's +0x54
  is 0. The same scratch run with one 001EFD90(0x80000009) (subtype 6)
  added faulted at 001EF510 and stopped the game task. None is reachable in
  AREA11 in the port:

  | Handler (id) | Spawned by | Why AREA11 does not reach it |
  |---|---|---|
  | 001EAF00 (0x80000005), 001EB980 (0x68), 001EC5F0 (0x66), 001EC820 (0x67) | the footstep effect 00187EE0 on surfaces 6 / 7 / 8 / 0x5C; the slide 0016CD70 on 8 / 0x5C | the surfaces AREA11 can put in +23A are 0, 3, 4, 5, 0x5A, 0x5D (grid census) and 0, 3, 4, 0xB, 0xD (class-4 owners); surface 8 exists only as a static-cell kind, and AREA11's collision directory has no static cell (SFX_REGISTRY_FIRST_LEVEL.md, em_collision_world.c) |
  | 001EAF80 (0x8000001D), 001EB020 (0x16) | 00187350's wading ripple (+23C set) and 00187EE0 on 0x5B; the climb grab 0017DEB0 with +23C set | +23C is set (1 / 2) only by 00175900 on surface 0x5B: the split listing's other stores to a +0x23C are 001647D0's clear and 001551B0's / 001D7BB0's words into their own records, and AREA11 has no 0x5B |
  | 001ED450 (0x80000023) | the blast reaction 0021EAD0 / 0021EF30 (+5 0x12..0x14) | entered only from the hit requests +F 7 / 0xA / 0xB; no live port code writes +F (only clears it) |
  | 001EBC30 (0x14), 001EBD20 (0x15), 001ED7A0 (0x07), 001EB600 (0x5F) | the crates' and drums' break and flight | reached only after a damage write to the owner's +0x36; no live port code writes it (CRATES_DRUMS_ORIGINAL.md) |
  | 001EBBB0 (0x8000000E) | 001F0460's preset 0 | 001F0460 (the footstep decal) faults before it (below) |
  | 001EF510 (0x09) and every other subtype | no first-level spawn site | — |

**001EFE00** (em_player_misc_001EFE00; `w_attach` / `mw_spawn` in
em_player_closure_live.c) faults as the stage's w001EFE00 already did
(em_player_stage_live): its 001EF9D0 node view (+0x24, +0xB0, +0xC0) is not
bound. Its callers are the hit and death paths (0021C120 0x80000040, 0021C200
0x80000048, 0021CD9C 0x80000044 on +23B 0xA, major2's 0x80000051), none
reachable while no live code writes a hit request, and the callbacks of all
those ids (0022BBC0, 001F8350, 0021AE90) have no AREA11 binding row, so the
node would fault in `bind_node` anyway.

Not modelled on purpose (each faults when reached; none is reached on the
route, route census `route_functions.json`): the footstep decal 001F0460
(00187EE0 copies a stack word its 001031E0 never writes into the matrix's
row 3), 001CD390's subtypes.

### 8.3 What is not drawn

The chains are built byte for byte in the context's chain table, which
001D1EA0's 001CB800 splices every frame; no port renderer stage consumes the
effect chains (the puffs, the head sprites' breath, the glint, the glow
markers, the ring lanes). The AREA11 effect owner and the snow keep their
own projection path (em_effect_sprite_project / em_snow_project over
em_snow_particles_generate), now with the context's fog. Drawing the chains
is the renderer's (the VU1 programs of table 0x231770 / 0x233290; WP-13).

### 8.4 Evidence

- **The captures** (`tools/test_level_smoke.py` check_effects, LEVEL_SMOKE.md):
  at the port ticks the phase checks align with the last rows of routes 08,
  10, 11, 12, 13 and 14, the effect nodes (state, subtype, step, limit,
  accumulator; route 08's eight truck puffs with their +0xB0 and +0x100 bit
  for bit, seven of them at the snapshot's pool addresses; route 12's four
  player footstep puffs, subtype 5, from the player-side 001EFD90 spawns,
  aligned by the crevice_jump phase at its entry row + the rows to the
  capture's end), the head sprites (lifecycle, key, owner, bone, offset)
  and the equipment nodes equal the snapshots; the barrel's 24 lane packets
  equal the snapshot's latest DMA buffer (packets 1..3 in all six, the
  lane-3 parameter quadwords excepted, see below; packet 4, the view and
  fog, in 10 and 14 where the camera equals the capture's) and route 10's
  five visible glow-marker primitives equal the snapshot's (colour
  excepted: rand()). Over the whole run every barrel frame emitted the 11
  markers and the six lanes, and no counted gap.
- **Not compared, and why:**
  - the head sprite's sub-state +0x05: it flips when the wait +0x1F0 =
    00122BB8() % 40 + 60 runs out, so it follows rand() like the ramp
    +0x244 / +0x24C; at the aligned ticks it differs in routes 08, 10, 11,
    12 and 13 and agrees only in 14;
  - the lane-3 parameter quadwords (+0x40 of each of lane 3's 32 slots,
    ring 0x76D9C0): 001F03D0 leaves +0x40 as it finds it, and those bytes
    (20 of 32 slots nonzero; lanes 0, 1, 4, 5 and 6 all zero) are identical
    in opening_ee.bin, handoff_ee.bin, playable_ee.bin and every route
    capture 00..14, so nothing during the level writes them. **Open:** no
    capture from before the AREA11 opening exists, so which earlier routine
    (title, New Game, the area load) wrote them is not shown; a title-screen
    or New Game capture would decide it (a new capture for the lead).
- **Coverage** (a `-fprofile-instr-generate` build of the live link line,
  scratch, full-route smoke and side beat 00): 001F0360 14,223, 001EF9D0 131,
  001EA240 7,231, the handlers 7,106, 001CFBE0 25,318, 001E2560 28,446,
  001F0A60 1,067, 001CD520 156,453, 001F0310 2 (census 1.17).
- **The oracles** of every bound translation are unchanged
  (test_effect_manager_reference, test_effect_original_reference,
  test_effect_kinds_reference, test_head_sprite_reference,
  test_player_equipment_reference, test_packet_chain_reference).
- **The fog stand-ins:** test-snow-runtime and test-area11-effect-runtime run
  the real 0021B9A0 on a fixture context (tests/render_context_frame_stub.h);
  the snow leaves the mode-1 pair (the area's) as 001E67C0 does. The level
  smoke's check_render_context still holds the snapshots' fog block
  +0xA0..+0xFF at every gameplay tick's end.
