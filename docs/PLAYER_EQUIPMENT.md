# Player equipment: the seven 0018A6B0 nodes, 0015D2F0 and 001CD520

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "player-equipment" (census gap lane **L28-player-equipment**,
docs/FIRST_LEVEL_CENSUS.md section 5). This document covers what the
original routines do, the translations `src/game/em_player_equipment.c/.h`
and `src/game/em_player_equipment_sprite.c/.h`, how they bind, and the
evidence. **Live since 2026-09-25** (census L26 / L27 / L28 / L39 step):
`em_equipment_live` binds the seven nodes and `em_effects_live` binds
001CD520 (section 4 is the binding, section 8 the live evidence).

## 1. The lane's census rows

| Function | Decomp | Census before | After this lane | Where |
|---|---|---|---|---|
| 0018A6B0 node behaviour | BM | missing | translated, live | `em_player_equipment_tick` |
| 0018A8D0 node init | BM | missing | translated, live | `em_player_equipment_0018A8D0` |
| 00188630 flavour 0 | BM | stand-in (em_weapon.c `em_weapon_update`) | translated, live | `em_player_equipment_00188630` |
| 00188A50 flavour 1 dispatch | BM | missing | translated, live | `em_player_equipment_00188A50` |
| 00188AC0 flavour 1 variant 0 | BM | missing | translated, live | `em_player_equipment_00188AC0` |
| 00188B80 flavour 1 variant 0x10 | BM | missing | translated, live | `em_player_equipment_00188B80` |
| 00188DF0 flavour 2 dispatch | BM | missing | translated, live | `em_player_equipment_00188DF0` |
| 00188ED0 flavour 2 variant 0 | BM | stand-in (em_weapon.c lamp gate + em_gfx spot term) | translated, live | `em_player_equipment_00188ED0` |
| 0018A1F0 flavour 4 | BM | missing | translated, live | `em_player_equipment_0018A1F0` |
| 00189D30 flavour 4 effect step | BM | missing | translated, live | `em_player_equipment_00189D30` |
| 0015D2F0 camera-mode code | BM | stand-in (em_weapon.c assumes variant 0) | translated, live | `em_player_equipment_0015D2F0` |
| 001CD520 depth-faded sprite | NEARMISS (21.9%) | unverified (em_status_models.c `w_001CD520`, a fault) | translated from the listing, live | `em_player_equipment_001CD520` |
| 001607D0 action machine | BM | verified-unbound | unchanged: binding notes only (section 4.6) | `em_player_weapon_001607D0` (em_player_weapon_states_a) |

"Verified" means: `tools/test_player_equipment_reference.py` executes the
original instructions and compares every modelled byte, every worker call
and every result (section 6). The three plain copies the routines call
(copy_qw4 00102958, 00102948, 001031E0) are translated inline and run
unhooked in the oracle.

## 2. What the original does

The seven nodes are spawned by 0018A880(flavour, variant) (pool class 1,
callback 0018A6B0): 0015C420 spawns (4, 0) and stores it at player +0x18;
0015C310(player, 0) spawns (0, 0) (stored at player +0x20), (1, 0), (1, 0x10)
and three flavour-2 nodes from D_00810CA4..CA7 (on the route (2, 5), (2, 0),
(2, 7)); with D_00810CA6 == 4 also (1, 0x15). Every route capture holds
exactly these seven, all in lifecycle 1. "Bone i" below is the 64-byte world
matrix at +0x90 of the node D_00275B40[i] (the bone work array the pool walk
publishes as the current record's +0x110 slots before each callback); "player
bone s" is the one at the player's +0x110 slot s (4 = +0x120, 14 = +0x148,
19 = +0x15C).

### 0018A6B0(node): the behaviour

Lifecycle +0x04:
- **0:** 0018A8D0(node, 0); when it returns 0, +0x04 = 1.
- **1:** +0x01 = 1 when player +0x01 != 0 or player +0x1F0 == 0x33. Then by
  flavour +0x03: 0 → 00188630; 1 → 00188A50; 2 → when D_008106CC != 0
  (an equipment change is pending): if +0x0D == 0 and D_00810CA6 != 0 and
  D_008106C7 != 0, D_008106C7 = 0; then +0x04 = 3 — otherwise 00188DF0;
  4 → 0018A1F0; other flavours nothing. Last, the draw method at +0x4C
  (001CAA00 on the route) runs when +0x01 != 0, unless D_008106C6 != 0 and
  player +0x230 == 0xC and player +0x1F1 == 1.
- **2:** nothing. **3:** 001AFC10(node) (free).

### 0018A8D0(node, id): init

Picks the model id from (+0x03, +0x0D): flavour 0 → 0x2F (and +0x2E = 0,
+0x210 = +0x214 = 0); flavour 1 → 0x30 / 0x40 / 0x6D for variants 0 / 0x10 /
0x15; flavour 2 → 0x32, 0x33, 0x34, 0x35, 0x36, 0x31, 0x37, 0x38 for variants
0..7 and 0x39..0x3D for 8..12, which also set D_008106C6 = 1..5; flavour 4
→ 0x6A (and +0x28 = 0). An unlisted variant keeps the caller's `id`
(0018A6B0 passes its +0x04 byte, 0); any other flavour returns 1 at once.
Then 001CA6E0(node, 001C6120(D_0028A56C, id)) binds the model,
+0x0C = 001C6150(+0x44) (the bone count); if the signed D_00275BCC is below
it, returns 1; otherwise pulls +0x0C slots with 001AF780 into +0x110..,
+0x09 = +0x0C, anim_bone_array_setup(+0x0C), bone_init_default_1(node),
returns 0.

### 00188630(node): flavour 0

- Bone 0 = player bone 4.
- sel = 7 when player +0x275 (the camera mode) is 0 and D_00810CA4 == 0,
  6 when mode 0 and D_00810CA4 == 2, else the mode.
- 0x70003600 = (−3.0, word 1 of D_0024A220 row sel, 0, 1.0);
  +0xA0 = 001026A0(player +0x2A0 matrix, that point); +0xB0 =
  001026A0(player +0x2A0, D_0024A220 row sel); +0xC0 = +0xB0 − +0xA0
  (001028D0), normalised in place (00102760).
- +0x1F0 = 001026A0(bone 0, D_0024A2A0 row mode); +0x210 = 0.
- Player +0x1F0 in {0x32, 0x35} with +0x1F1 == 1: 001854E0(node) when
  player +0x318 == 0, else 00185760(node). Player +0x1F0 in {0x31, 0x34}
  with +0x1F1 == 1 and player +0x2F2 != 0: the same two, swapped.
- Then, re-reading the mode, the one-shot on +0x2E:
  - **0:** if pending, clear it; 001861C0, 00187CC0; 0x700036A0 = bone 0;
    0x700036D0 = 001026A0(bone 0, D_0024A300 row mode);
    001F4010(3, 0x700036A0).
  - **1, 2:** if pending, clear; 001869A0; 001B61C0(0, 0xE8, 0xF, 1).
  - **3:** if pending, clear; 00186A60, 00187CC0; 001B61C0(0, 0xD8, 0xC, 1).
  - **4:** only when +0x2E == 1: 0x700036A0 = bone 0; 0x700036E0 =
    identity (001029C0), rotated by π/2 (00102BB0); 0x700036A0 =
    001026D0(0x700036A0, 0x700036E0); 0x700036D0.xyz = +0xB0.xyz;
    001EFEB0(0x80000039, 0x700036A0); 001B61C0(0, 0x65, 5, 1). +0x2E = 0
    in every case.
  - **5:** if pending, clear; 001872C0; 001B61C0(1, 0xF8, 0x12, 1).
  - Other modes: nothing.

### 00188A50 / 00188AC0 / 00188B80: flavour 1

00188A50 dispatches +0x0D: 0 → 00188AC0, 0x10 → 00188B80, 0x15 → 00188C70.

00188AC0: if D_00810CA4 is 0 or 2, +0x01 = 0, else bone 0 = player bone 4.
Then +0x05: **0** → +0x05 = 1 when D_008106CC != 0; **1** → D_008106C6 = 0,
0015C310(player, 1) (spawns the new flavour-2 children), D_008106CC = 0,
+0x05 = 0. This is the equipment-change respawn: the flavour-2 nodes free
themselves in 0018A6B0 while D_008106CC is set.

00188B80: bone 0 = player bone 19 when player +0x04 == 1, +0x275 == 0,
+0x1F0 == 0x33, the clip +0x20C equals D_00248B98[0] or D_00248C78[0], and
the clip time +0x3C is not below 12.0 and at most 51.0 (EE COP1 compares);
otherwise player bone 4.

### 00188DF0 / 00188ED0: flavour 2

00188DF0 dispatches +0x0D: 0 → 00188ED0; 1 / 2 / 3 / 0xC → 00189090 /
00189330 / 001899C0 / 00189A20; any other → +0xD0 = player bone 4, then
001C9610(D_00275B40, +0x0C, +0xD0) (the placement over its bones).

00188ED0: bones 0..+0x0C−1 = player bone 4. Then, when D_008106C7 != 0 (the
lamp is on): if (player +0x04 == 1 and +0x05 in {0x1D, 0x1E, 0x1F, 0x20})
or (+0x04 == 2 and +0x05 in {0x17, 0x18}): 0x700038C0 = (3.6, 0.5, 0, 1.0),
+0xB0 = 001026A0(bone 0, 0x700038C0), and 00187780(node, a, b) with a = 0
when 001B0070() has bit 0x80 else 1, b = 0 when player +0x1F0 is 0x31 or
0x34 else 1. Otherwise D_008106C7 = 0.

### 0018A1F0 / 00189D30: flavour 4

0018A1F0: bone 0 = player bone 14. While +0x00 bit 0 is set (the pool status
the attack code raises):
- 001AA840(node); 0x700038A0 = 001026A0(bone 0, (0.25, 1.0, 0, 1.0));
  0019B2C0(player +0xB0, 0x700038A0, 6). On a hit, when the hit entity
  (0x700031D4) is 0, or 00189EC0(entity) returns 0, and the hit face record
  (0x700031D0) has (+0x1A & 0xFF00) == 0x2000: 0x700038C0.xyz =
  0x700031B0.xyz, 0x700038B0 = (face +0x24, +0x28, +0x2C, 1.0), 0x700038A0 =
  0x700031B0, 001F00A0(0x80000003, 0x700038A0, 0x700038B0, 0), 0018A180(node).
- If bit 0 is still set (0018A180 writes +0x00 = 2): seven segment probes
  0019A570(from, to, 7, 0x20), each reporting a result of 1 or 2 to
  00189FE0(node, to, from): (D_0024A440 row 0, row 1) through bone 0; rows
  2..3 and 4..5 added to both ends (001028B8), staged in 0x700038A0/B0 and
  0x700038C0/D0; last from = player +0xB0 with y = the D_0024A4A0 point's y,
  to = that point.
- Then 00189D30.

00189D30, on +0x07: **0** → when +0x00 == 1: +0x07 = 1, 0x700038A0..DC =
(0, 0, 0, 1), (0, 5, 0, 1), (64, 64, 64, 128), (0, 0, 0, 0), and +0x20 =
001EFF10(0x8000000D, bone 0 of *(+0x14) (the node itself), those four,
f12 = 10.0). **1** → when +0x00 == 2: effect +0x04 = 2, +0x07 −= 1; else when
player +0x1F0 is neither 0x36 nor 0x37: effect +0x04 = 2, +0x00 = 2,
+0x07 −= 1.

### 0015D2F0(): the camera-mode code

0 unless player +0x04 == 1. Then by player +0x05: 29/31 → with +0x1F1 == 1:
2 if +0x318 == 1, 0 if 3, else 1; otherwise 1 if +0x318 == 2 else 0. 30/32 →
with +0x1F1 == 1: 1 if +0x318 == 1, 0 if 3, else 2; otherwise 2 if +0x318 == 2
else 0. 35 → 0x82 if +0x1F1 == 1 else 0. 25 → 3. Anything else → 0.

### 001CD520(bucket, mode, point, giftag, w, h, zbias, rgba): the sprite

Read from the split listing; the NEARMISS C is not followed (section 5).
1. The point (w lane ignored, 1.0 used) is transformed by the 001CD370(0)
   matrix; if any of the six clip flags of xyz against |w| is set, it
   returns 0x00FFFFFF and writes nothing.
2. The point through the 0x70003AC0 matrix; x, y divided by w; w −= zbias;
   z divided by the new w; w = fog.z + fog.w · w, clamped to at most fog.x
   and at least +0 (fog = the four words at *D_00275670 + 0xA0); all four to
   28.4 fixed point at 0x70003600..0C.
3. 0x70003610/14/18 = 0.5·w, 0.5·h, the step-2 clip w; that vector through
   the 0x70003A40 matrix with its third row's x and y zeroed; x, y divided by
   w and converted to 28.4; the full quadword (x, y as integers, z and w as
   the transform's floats) is stored back at 0x70003610..1C.
4. mode != 0: fog8 = (0x7000360C >> 4) clamped to 0xFF. Mode 1: alpha =
   alpha · fog8 >> 8. Modes 2, 3, 4: each of r, g, b · fog8 >> 8, and
   0x7000360C = 0xFF0. Other modes: no change.
5. chain = D_0028F700 + (bucket << 15) + 0x4D3EC0; 001CB5F0(chain, z, 6)
   opens six quadwords: +0x00 giftag (8 bytes), +0x10..1C r, g, b, a words,
   +0x20 0, +0x24 0, +0x28 1.0, +0x30 x + ex, +0x34 y + ey, +0x38 z, +0x3C
   fog, +0x40..48 1.0, +0x50 x − ex, +0x54 y − ey, +0x58 z, +0x5C fog
   (+0x08..0F, +0x2C and +0x4C untouched). Then 001CB6B0(chain, z, 2,
   D_00251220) and 001CB900(chain, z, mode); returns z.

## 3. The translation

- **Records.** `EmPlayerEquipmentNode` holds exactly the node bytes the
  routines read or write, each at its original offset, with native pointers
  for +0x14 (self), +0x20 (the effect record, whose byte +4 is written) and
  the +0x110 bone slots (`EmOwnerBone *`, the owner-services bone type, so
  001C9610 and 001CAA00 bind directly). +0x44 (model) is an opaque handle
  and +0x4C the method's original address.
- **Views** (`EmPlayerEquipmentWorld`): the 0x320-byte player record image
  (D_008102B0), the player's bone slots, the bone work array D_00275B40
  (read at every use, as the original reloads the global: a worker that
  republishes it must update the field), D_008106C6/C7/CC, D_00810CA4/CA6,
  D_00275BCC, D_0028A56C, the first halfwords of D_00248B98/D_00248C78, the
  ELF data window from D_0024A220 (rows at 0x24A220, 0x24A2A0, 0x24A300,
  0x24A440..0x24A4A0; an access outside the given words faults), the
  scratchpad windows 0x70003600..0x7000371F and 0x700038A0..0x700038DF (the
  binder hands the same storage to every worker that uses them), and the
  collision result 0x700031B0/D0/D4.
- **Workers.** Every callee that is not translated here is a worker named
  by its address. The SDK VU0 routines take their inputs by value and return
  their result, which the translation stores at the original destination;
  this equals the original because each of them loads all its inputs before
  its first store (the matrix product 001026D0 loads the whole first matrix
  first, and the only aliased call, 001026D0(0x700036A0, 0x700036A0,
  0x700036E0), aliases that one). Callees whose read extent is not known
  (001EFEB0, 001EFF10, 001F00A0, 001F4010) get pointers into the owned
  scratchpad windows.
- **Fail-stop.** Each entry checks, before its first write, every worker and
  view it can reach (0018A6B0 reaches all of them). A negative worker result,
  a NULL bone slot or effect record the original would dereference (the
  original writes address 4 in 00189D30 step 1 with no effect record), a NULL
  hit face record after a hit, a table row outside the window (in the
  window given by section 4.3, a camera mode above 32) or a bone count above 56 (the original would overwrite +0x1F0)
  latches a fault and returns −1; later calls return −1.
- **Float.** 00188B80's two compares use `em_ee_c_lt_bits` / `em_ee_c_le_bits`.
  001CD520 runs every VU0 macro instruction through `em_vu_vec_bits` /
  `em_vu_div_bits` / `em_vu_ftoi4_bits` / `em_vu_min_bits` / `em_vu_max_bits`
  under its real form, and 0.5·w / 0.5·h through `em_ee_mul_bits`. Its clip
  test is not in the measured model (see section 7).

## 4. Binding (live since 2026-09-25: em_equipment_live)

`src/game/em_equipment_live.{h,c}` binds the translation; it adds no
behaviour of its own.

### 4.1 The node callback

em_area11_bindings' row 0x0018A6B0 ticks `em_equipment_live_tick` at the
node's walk position (001AFD70). The binder keeps one
`EmPlayerEquipmentNode` beside each pool `EmActor` (status +0x00, +0x01,
+0x03 = the `model` byte, +0x0D = `param`, +0x14 = the record), republishes
the node's bone slots as D_00275B40 before the call (001CB590's publication)
and writes the node's bytes back after it. 0015C310's spawn 0018A880 is
the bindings' `spawn_0018A880`; 001AFC10's 001AF800 on one of these nodes
pushes its slots back (`em_equipment_live_001AF800`, 001AF890).

### 4.2 Workers

| Worker | Bound to |
|---|---|
| w_001AFC10 | `em_actor_pool_free_001AFC10` |
| w_method (0x001CAA00) | the renderer's boundary: the seven models are drawn by the player's mesh at its node slots 4 and 14 (the export's attachments); the method records that the node drew and whether its bone 0 is the node that mesh draws it at (section 8) |
| w_001C6120 / w_001C6150 | `em_area11_roger_001C6120` over D_0028A56C (the Roger export's global table 0x37 and the equipment models, `tools/export_roger_banks.py`), the model's +0x08 |
| w_001CA6E0, w_anim_bone_array_setup | `em_roger_actor_001CA6E0` over the parsed model and skeleton |
| w_001AF780 | `em_roger_actor_001AF780` on the one 001AF710 bone-slot stack (em_area11_boxes' world) |
| w_bone_init_default_1, w_001C9610 | `em_owner_services_001C62C0`, `em_owner_services_001C9610` |
| w_001026A0, w_00102760, w_001026D0, w_001028B8, w_001028D0, w_001029C0, w_00102BB0 | em_effect_original's 001026A0 / 00102760, `em_loco_001026D0`, `em_player_hang_vadd`, VSUB.xyzw (em_ee_float.h), em_owner_services' identity / rotate-y |
| w_001B0070 | D_008106C8 (the request block) |
| w_0015C310 | the bindings' 0015C310 (`em_area11_spawn_player_equipment_0015C310`; arg1 = 1 from the equipment change D_008106CC, which the status page writes) |
| w_001B61C0, w_0019A570 and every untranslated callee: w_001854E0, w_00185760, w_001861C0, w_001869A0, w_00186A60, w_001872C0, w_00187CC0, w_001EFEB0, w_001F4010, w_00188C70, w_00189090, w_00189330, w_001899C0, w_00189A20, w_00187780, w_001AA840, w_0019B2C0, w_00189EC0, w_001F00A0, w_0018A180, w_00189FE0, w_001EFF10 | faults (none ran in any census label; 001B61C0 / 0019A570 are reached only from the untranslated ones) |

None of the faulting callees can run in the port today: the aim selector
needs the armed stances, which the port's stand-ins still own (L28,
P24..P28); the lamp 00187780 needs D_008106C7, which only the port's
em_weapon.c raises in its own storage (the request block's byte stays 0,
as in every capture); the knife's bit 0 and a camera mode other than 0 are
not reachable on the idle / walk states.

### 4.3 Data

- The ELF window D_0024A220..D_0024A4AF and the halfwords D_00248B98 /
  D_00248C78: `assets/effect_tables.emet` (`tools/export_effect_tables.py`,
  read through `em_effects_live_elf`).
- D_008106C6 / C7 / CC: the scene state's request block
  (`em_scene_req_at`); D_00810CA4 / CA6: its progress block.
- The player record image (`player_states_actor_mut`) and its bone world
  matrices (the record pose's node records, copied every tick); D_00275BCC
  and the bone-slot stack: em_area11_boxes' 001AF710 world.

### 4.4 Stand-ins still in place

- **00188630** (em_weapon.c's laser gate): the flavour-0 node runs, but its
  selector 001854E0 / 00185760 needs the armed stances the port does not
  run yet; em_weapon.c's laser and muzzle code stay until they do.
- **00188ED0** (em_weapon.c lamp + `em_gfx_spot_light`): the node's lamp
  gate runs over the request block's D_008106C7; the lamp itself is 00187780
  (not translated).
- **0015D2F0**: the node calls it live; 001D1C50's, 00187CC0's, 001DDA00's
  and 001DDE10's calls are their own lanes'.

### 4.5 001CD520

`em_effects_live` binds `em_player_equipment_001CD520` as 001F4D40's sprite
(the glow markers) over the render context's packet chain
(EFFECT_MANAGER.md section 8). em_status_models' `w_001CD520` still faults
(no status-model caller reaches it).

### 4.6 001607D0

Already translated and verified (`em_player_weapon_001607D0`,
`tools/test_player_weapon_states_a_reference.py`). It is the player's action
machine, called by 00161020 / 001612D0 (idle / walk) and the stance routines,
so it binds with the locomotion lanes (L01, L12), not with the equipment
nodes.

## 5. 001CD520 and its NEARMISS C

The decomp C (21.9%) is not the reference here. Following the listing, the
translation differs from that C where the C is wrong or incomplete:
- the second quadword store (after the half-extent projection) writes all
  four words 0x70003610..1C (x, y as 28.4 integers, z and w as floats); the
  C writes only x and y;
- the half-extent transform's w lane uses the full four-row form, and its
  input is the whole quadword at 0x70003610 (the fourth word is not used
  because the transform takes w from the constant 1.0);
- only the low 32 bits of the colour register reach any store; the mode-1
  path's 64-bit masking and sign extension do not change them.

## 6. Verification

`make test-player-equipment-reference` (hunk in the lane report) runs
`tools/test_player_equipment_reference.py`:
- **Callee set:** the 57 jal targets of the 12 translated routines are
  exactly the translated routines, the three inline copies and the hooks.
- **Unit cases** (the ELF's RAM image plus randomised node, player, globals,
  scratchpad, bone and face records): every entry point; both sides get the
  same scripted worker effects; the SDK VU0 hooks run the original routine
  on the recorded inputs on both sides. Compared: every modelled node byte,
  every bone world matrix, the effect record bytes, D_008106C6/C7/CC, the
  whole player image, both scratchpad windows, the hit point, the worker
  call sequence with arguments (pointer arguments as their original
  addresses and contents) and the result. Every byte the original stores
  must be one of the compared bytes.
- **Branch coverage:** all 254 conditional-branch outcomes of the 12
  routines are reached except two that no input can produce, and the test
  fails if either of those ever occurs:
  - 00188ED0 (0x189064): the second D_008106C7 != 0 test cannot fail
    because nothing clears it after the entry test;
  - 001CD520 (0x1CD6E4): fog >> 4 cannot be negative because the fog word
    is clamped at +0 before the conversion.
- **Route:** the seven live nodes of all 15 captures in
  `../Extermination/build/s87/route/` are ticked over the captured RAM (the
  walk's D_00275B40 publication applied), once as captured (lifecycle 1:
  flavours (0,0), (1,0), (1,0x10), (2,0), (2,5), (2,7), (4,0)) and once
  from lifecycle 0 (the 0018A8D0 init over the real records).
- **001CD520:** 25 cases per beat on 4 captures (60 on all 15 in full mode)
  with the captured camera, projection and fog, points around the player
  (drawn) and far away (culled), every mode including −1 and 5, and a fog
  quad reaching past 255. A non-finite point must fault natively.
- **0015D2F0:** 900 of the 15,360 (+0x04, +0x05, +0x1F1, +0x318)
  combinations; all of them in full mode.
- **Fail-stop:** an unbound worker set faults before any write and the
  latch refuses later calls; camera mode 255 faults (table window); a
  missing effect record in 00189D30 step 1 faults.
- **Sensitivity:** ten hand mutations of the two C files (a wrong bone slot,
  a dropped condition, an off-by-one range, a wrong constant, a wrong lane,
  a skipped re-read, a wrong fog lane, a wrong packet word, a short store,
  an inverted test) each fail the default run.

Default run: about 5 s (quick mode: 1,000 unit cases). `EM_TEST_FULL=1`:
6,000 unit cases, every 0015D2F0 combination and 915 sprite cases, about
25 s. Both pass.

## 7. Limits

- The clip test of 001CD520 is not part of the measured float model
  (docs/EE_FLOAT_MODEL.md). Both sides use the exact comparison of DAZ'd
  finite operands (the precedent of em_effect_original.c's 001CCF70). A
  non-finite operand faults natively and is not compared.
- The workers' scripted effects are chosen by the test; they exercise the
  translation's re-reads (the camera mode after 001854E0/00185760, the
  status byte after 0018A180, the scratchpad after 00189FE0, a republished
  D_00275B40 after 001861C0/00187CC0), not the workers themselves.
- The route captures are snapshots at the end of each beat: the nodes are
  ticked from those states, not replayed through the beat. No capture has
  the lamp on, a knife hit, an equipment change or a camera mode other
  than 0.
- The untranslated callees in section 4.2 have no port code; they are
  faulting stubs, safe because neither the route nor any state the port
  can enter today reaches them (4.2).
- The nodes' own draw (001CAA00 over each model) is not the port's: the
  player's baked mesh draws the equipment at its node slots 4 and 14.

## 8. Live evidence (2026-09-25)

- **Captures** (the level smoke's check_effects, LEVEL_SMOKE.md): at the
  ticks aligned with the last rows of routes 08, 10, 11, 13 and 14 the seven
  nodes' bytes +0x00..+0x0F (the flavours (0,0), (1,0), (1,0x10), (2,0),
  (2,5), (2,7), (4,0)), their +0x44 model and +0x4C method equal the
  snapshots; the tick log carries each node's drew / at-node flags and its
  bone 0 row 3, and at those ticks every node drew at the player's node
  its mesh draws it at.
- **Coverage** (a `-fprofile-instr-generate` build of the live link line,
  scratch, full-route smoke and side beat 00): the node tick 99,570,
  0018A8D0 23, 0015C310(p, 1) 3, 001C9610 28,430, 001CD520 156,453.
- **Draw position:** the method reports once when a node draws with a
  bone 0 that is not the player's node the mesh uses; the only case seen is
  a flavour-2 node in the tick it frees itself (the equipment change): the
  byte-matched tick calls the method with the node's last matrix in that
  tick.
- The oracle `make test-player-equipment-reference` is unchanged.
