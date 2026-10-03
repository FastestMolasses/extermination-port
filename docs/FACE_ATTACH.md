# Face attachment draw (001CB3C0) — Roger's face and the player's

Status (2026-09-28, chain C8b FACE): **live.** The +0x90 attachment draw of
the 001CAA00 owners runs from em_owner_draw_live's `w_001CB3C0` for Roger
(008237E0, every frame his +0x4C runs; since chain C8b OPENING also the
opening's 001BB0E0 body) and for the player while a script holds its face
slot (001B81D0's 001CA700 .. 001B82D0 sub 4's 001CA770, now
on the player record's own +0x90 slot, `em_face_slot`). The face unit it
appends is parsed with the owner's unit and drawn by the object-unit
renderer's face-morph program (OWNER_DRAW.md section 8 F). Roger's and his
equipment's legacy meshes and the player's face host are retired. Section 7
is the live proof.

| File | Role |
|---|---|
| `src/game/em_face_attach.{h,c}` | 001CB3C0, 001D3F50, 001D3E40 (with em_owner_draw_original's vif_append_ref_tag / tag writer and a 001D2910 worker), and `em_face_attach_w_001D88B0`, the adapter that runs the existing 001D88B0 translation as the EmAnimRest worker |
| `src/game/em_owner_draw_live.c` | the binding: `w_001CB3C0`, the owner + face unit parse, `em_owner_draw_live_001CAA00_attached` (section 6.1) |
| `src/game/em_face_slot.{h,c}` | the player's face slot: 001CA700 / 001D06D0 / 001D06E0 / 001CA770 / 001D0C70 on the record's +0x90 over em_roger_actor_original (section 6.3) |
| `tools/test_face_attach_reference.py` | The original-instruction oracle of 001CB3C0 (section 4) |
| `tools/test_face_slot_reference.py` | The original-instruction oracle of the face slot sequence (section 6.3) |
| `tools/level_smoke_face.py` | The level smoke's live check (section 7) |
| `docs/FACE_ATTACH.md` | This file |

## 1. What the originals do

**001CB3C0(owner)** (byte-matched C) is called by 001CAA00 after 001CA990,
whenever owner +0x90 is nonzero. **This includes owners that 001CA7B0
culled**: the face unit is appended even when the body unit is not.

1. It reads p = owner +0x90 (the face slot) first. The slot's +0x60 is read
   only at step 7, after 001D1F80.
2. It builds a local 4x4 matrix with 001029C0 (the identity). Row 3 xyz
   become the three words D_00250FB0, D_00250FB4 and D_00250FB8 (.data of the
   boot ELF, copied raw).
3. It calls 001026D0(local, D_00275B40[owner +0x94] + 0x90, local). The node
   index is the signed halfword +0x94 (7 for every captured face). The node's
   world matrix rows are loaded first; then each local row goes through
   them, so local = local × node.
4. It calls 001C7900(local, owner + 0x80, 0x3F5, 0). That call does the
   following:
   - 001D88B0 builds the lighting matrices A (0x70003400) and B (0x70003440)
     from the owner's +0x80 colour.
   - It appends a CNT of B to VU address 0x3F5.
   - It appends a CNT of the local rows × the view-projection (0x70003AC0),
     followed by the normalised rows × A, to VU address 0.

   ANIM_RUNTIME_REST.md section 2 has the details.
5. It calls 001CB2C0(owner, 0x3F3, 0). This appends a CNT of the slot's two
   weight quadwords (+0x40 and +0x50) to VU address 0x3F3.
6. It calls 001D1F80(0, 1, 0), which appends the GS state REF 9 to
   D_00815360 (class 0).
7. It calls 001D3F50(*(p + 0x60)). The slot's +0x60 holds the face
   resource, D_0028A490[row].

**001D88B0(position, A, B, token)** as 001C7900 calls it. Its translation
already existed as em_frh_001D88B0 (FRAME_RENDER_HEADS.md 2):

- In modes 1 and 3..6 (the context +0x246C word), it runs 001D8C30.
- In every other mode, it takes the rig path:
  1. D_00275688 = 0x00817BC0.
  2. 001D8130 reloads the room rig.
  3. 001D8340 runs with owner 0 and flag 0x20: slot directions, with the
     **camera fill always on** (transpose(D_00810610) × the rotated slot-0
     vector). Owner 0 returns before the point-light fold, so the position
     is never read.
  4. 001D8690(A, B, owner + 0x80, 0x20).

The shared rig record keeps what this call wrote, as it does in the original.

**001D3F50(face)** is a tail jump to 001D3E40(0, face).

**001D3E40(chan, face)** (NEARMISS C; the .s was followed) appends these
tags, in this order:

1. **vif_append_ref_tag(chan, 0x0023C480):**
   - a REF of 1 qword to *D_00275674 (the arena FLUSH qword);
   - context +0x50 + 4·chan = 0x0023C480;
   - a CALL tag (qwc 0) to the face program's chain.
2. A REF of 8 qwords to the skin record, D_00816440 + (context +0x9C) << 7.
3. **001D2910(0).** 0 is below 0x20, so this is 001D2710(0) = context +0x0C
   bit 0. While that bit is 0, it appends a REF of 2 qwords to D_002514B0
   (fog off).
4. A REF to face + 0x40. Its qwc is the low halfword of the face's +0x04
   word, which is read after the 001D2910 call and the optional REF 2.

Context +0x9C is read after vif_append_ref_tag, and the +0x0C bit after the
REF 8. The translation reads every one of these words at the original's
point (the fail-stop pre-checks only test that the addresses are mapped).

Every tag is written in the original store order: byte +3 (id), word +4
(address), halfword +0 (qwc), then the cursor advances by 0x10. Bytes +2 and
+8..+0xF are never written.

**The face unit** is therefore 0x190 bytes (0x1A0 with the fog-off REF):

| Part | Bytes |
|---|---|
| colour CNT | 0x60 |
| node CNT | 0xA0 |
| weights CNT | 0x40 |
| REF 9 | 0x10 |
| REF 1 | 0x10 |
| CALL | 0x10 |
| REF 8 | 0x10 |
| optional REF 2 | 0x10 |
| face REF | 0x10 |

## 2. Reused translations (no second copy)

| Original | Translation used |
|---|---|
| 001029C0 | `em_owner_services_identity_001029C0` |
| 001026D0 | `em_sdk_vu0_001026D0` (the aliasing dst == b is the original's) |
| 001C7900, 001CB2C0 | `em_anim_rest_001C7900` / `_001CB2C0` (ANIM_RUNTIME_REST.md) |
| 001D88B0, 001D8C30 | `em_frh_001D88B0` / `em_frh_001D8C30` (em_frame_render_heads), run by `em_face_attach_w_001D88B0` over EmFrh views of the light world, SPR A / B and the token bytes |
| 001D8130, 001D8340, 001D8690 | `em_actor_light_001D8130` / `_001D8340` (owner NULL) / `_001D8690` (the same rig record as 001D89D0) |
| 001D1F80 | a worker; live: em_owner_draw_live's cursor handover to `em_rcl_001D1F80`; the test binds `em_load_veil_particles_001D1F80` |
| vif_append_ref_tag (001D2090) and the tag writer | `em_owner_draw_vif_append_ref_tag` / `em_owner_draw_tag` (em_owner_draw_original, exported by this step; the module's inline copies are gone) |
| 001D2910(0) / 001D2710(0) | a worker: live `em_rcl_001D2910` (em_render_context_001D2910 over the one render context); the test binds `em_render_context_001D2910` over a view of the RAM copy. The inline context +0x0C test is gone. |

## 3. The native API

- `EmFaceAttachWorld` holds views only; the module keeps no copies:
  - **anim**: the one EmAnimRest. Its region table serves every EE-address
    read: record +0x80..+0x97, slot +0x40..+0x63 and face +0x04. Its channel
    and scratch are those of 001C7420 / 001CA940.
  - **draw**: the owner-draw world (context +0x9C / +0x50, D_00275674).
    Its channel must be the anim rest's.
  - **light**: the EmActorLight of 001D89D0 (the rig record, D_00275688,
    context +0x246C / +0x2380).
  - **context_address**: *D_00275670.
  - **d00250FB0**: 3 words.
  - **d00275B40** and its count: the walk's node array, the same as
    `EmOwnerServicesWorld.d00275B40`.
- `EmFaceAttachWorkers`:
  - `w_001D1F80`;
  - `w_001D2910(ctx, a0, &result)`: 001D2910. It is also queried, untraced,
    before the first write to size the unit (0x190 or 0x1A0); the query
    only reads context +0x0C;
  - an optional `trace(callee, a0..a3/t0, frame)`, called at every callee
    entry (tests only).
- `em_face_attach_001CB3C0(s, record_address)`, `_001D3F50`, `_001D3E40`
  and `em_face_attach_w_001D88B0`. `_001D3E40` takes a channel, but only
  channel 0 is verified: it is the one 001D3F50 passes and the only one any
  case runs. The last is bound as the EmAnimRest's
  `w_001D88B0` with ctx = the EmFaceAttach. 001CB3C0 refuses any other
  binding.
- **Fail-stop.** Everything is checked before the first write:
  - every view;
  - the regions (record, both weight quadwords at their & ~15 addresses,
    slot +0x60, face +0x04);
  - the node index (negative, at or past the count, or a NULL slot);
  - the light views the current mode will reach;
  - the channel room for the whole unit (0x190 / 0x1A0).

  These pre-checks only test that addresses are mapped; the words
  themselves (slot +0x60, face +0x04, context +0x9C / +0x0C) are read at the
  original's points (section 1). Any failure latches a fault and returns −1
  with nothing written. A callee
  that fails later latches WORKER at its address; the writes already made
  stay, in the original order.

## 4. Verification (`python3 tools/test_face_attach_reference.py`)

**Oracle.** The test runs the ORIGINAL 001CB3C0 on the EE of
test_anim_runtime_rest_reference: the shared slide core, the measured
COP1 / VU0 model, and the MMI PEXTLW / PEXTUW that 001D8340's 00102798
transpose executes. Nothing is hooked away. The setup:

- the captured RAM and scratchpad;
- D_00275B48 / 44 = owner and D_00275B40 = owner + 0x110, as 001CB590
  publishes them;
- the channel-0 cursor moved to a patterned window.

Pass-through recorders run at 13 callee entries:

- 001029C0, 001026D0, 001C7900, 001CB2C0, 001D1F80 and 001D3F50;
- inside 001C7900: 001D88B0, then 001D8130, 001D8340 and 001D8690;
- inside 001D3F50: 001D3E40, then vif_append_ref_tag and 001D2910.

Each recorder captures a **fixed subset** of storage (this path's whole
write set and the inputs its callees read):

- the argument registers;
- the display-list window and context +0x10 (the channel cursor);
- context +0x50..+0x5F;
- SPR 0x70003400..0x700034BF;
- the whole rig record (0x130 bytes, EM_ACTOR_LIGHT_RIG_WORDS) and
  D_00275688;
- 001CB3C0's stack matrix.

The whole 32 MB RAM and the scratchpad are compared only after the call,
not at each callee entry.

**Native.** The native side runs over a copy of the same image, and its
trace takes the same snapshot at each entry. The test compares:

- the callee sequence;
- every argument the native knows (the stack-matrix pointers are excluded;
  their contents are compared instead);
- the storage subset above at every entry;
- after the call, the whole 32 MB RAM and the scratchpad, byte for byte.

**Cases.** Every owner with draw method 001CAA00 and +0x90 != 0 in:

- route beats 00..14;
- the opening (Roger 0x7A8830, the second Roger actor 0x7A96E0, the player);
- the Roger encounter (Roger, the player);
- the C7 fence-door capture.

That is 21 owner-frames: 19 Roger, 2 Dennis. On top of these:

- **Synthetic variants** on Roger in beat 14 and the player in the
  encounter:
  - lighting modes 1..7 and −1;
  - the REF 2 path;
  - the 0x0F00 rig key;
  - skin slot 1;
  - nodes 0 and last;
  - an unaligned slot (+4);
  - the face +0x04 high half;
  - random node matrices, D_00250FB0 offsets, +0x80 colours and weights,
    including special words.
- **24 fail-stop checks** (the 001D2910 worker unbound is the 24th). The
  node array given to the native has a
  non-NULL guard entry one past the count, so the "+0x94 = count" check is
  refused by the index bound itself (pinned: BAD_INDEX at 0x00275B40), not
  by a NULL read past the end. A bound off by one (`>` for `>=`) fails it.
- **Coverage:** every reachable original instruction of 001CB3C0, 001D3F50,
  001D3E40, vif_append_ref_tag and 001D88B0 runs. A jump counts through its
  delay slot.

**Captured face units.** Each rebuilt unit is searched for in the capture's
own display list:

- **8 frames: equal in every written byte.** Beats 00, 01, 03, 08, 11, 13 and
  14, and C7 door 1.
- **12 frames: they differ only in lane x of the four lighting rows.** That
  lane is the camera-fill direction. In those snapshots D_00810610 already
  holds the next frame's view.
  - The 16 words at **0x00814040** hold the view the frame was drawn with.
    They equal D_00810610 in all 8 exact frames.
  - Rebuilt with those words as D_00810610, **all 12 equal the captured unit
    in every byte**, and the test asserts it.
  - The same 8 frames are the only ones where the player's own unit
    (0015C160's +0x4C) reproduces from the snapshot, which points to the same
    timing.
- **1 frame is not drawn:** the opening's first Roger has +0x01 = 0, and its
  unit is not in the list.

So **every drawn face unit of the captures is rebuilt byte for byte**
(routes 10 and 14 and the opening included).

**Through VU1.** Native units run through `em_object_unit_parse` +
`em_object_unit_run` and are checked against the ORIGINAL face program
(test_object_unit_reference's oracle); every triangle must be equal.

- **Quick run:** Roger in beat 14 (1,048 triangles), Dennis in the encounter
  (1,124) and a REF 2 unit.
- **Full run:** 40 distinct units, 30,104 triangles.
- The random-word variants are left out: the face program's fail-stop
  refuses an exponent-255 lane, which is a renderer contract.
- Roger's face kicks no triangle in beats 00..07 and 10, the encounter
  capture and C7 door 1. The original program gives the same (the full run
  passes these units through both).

**Results (M1):**

| Mode | Owner-frames | Synthetic | Callee entries | VU1 units | Wall | CPU |
|---|---|---|---|---|---|---|
| default | 21 | 30 of a pool of 42 | 645 | 3 | 2.2–5.6 s | 4.0–6.9 s |
| `EM_TEST_FULL=1` | 21 | 150 | 2,193 | 40 | 9.4 s | 32 s |

Timings are the binding step's reruns (2026-09-28); the default run is
slower when the private dylib is rebuilt.

Both modes use at most 4 workers.

**Defect injection** (run once while writing, not part of the suite). Each of
these one-token bugs fails the default run:

- an offset lane swapped;
- the 001026D0 operand order;
- the skin-record shift;
- the REF 2 condition inverted;
- the camera-fill flag cleared;
- the CALL written before the REF;
- a 12-bit qwc mask;
- the weights VU address.

`EM_FACE_ATTACH_SOURCE=<copy.c>` builds a mutated copy. The review's
bounded sweep of 44 single-operation mutants killed 43; the survivor (the
node-index bound `>=` → `>`) is killed since the guard entry above was added.
The offset-lane and 001026D0 operand-order mutants are caught only by the
random variants, because D_00250FB0 is zero in every capture.

## 5. Known gaps

- **The draw-time view.** The face unit is built at the owner's +0x4C in
  the walk, with the D_00810610 the walk's draws read (the one the previous
  tick's camera stage committed, as for the player's 001D89D0 camera fill);
  the sampled re-execution (section 7) runs the original over that same
  view, and the camera-exact snapshots 10 and 14 compare it with the
  capture's (OWNER_DRAW.md section 9).
- **VU registers.** 001C7900 leaves values in VU0 registers
  (ANIM_RUNTIME_REST.md 6); not modelled.
- **001D3E40 on channels other than 0** is not verified (section 3); no
  first-level caller passes another.
- **The opening.** Since chain C8b OPENING the opening's Roger (the
  script's class-9 001BB0E0 record, em_area11_roger's second pair) and the
  player draw their faces through this path too (OPENING_ORIGINAL.md); the
  level smoke's check_face samples the opening body's calls (record
  0x7A96E0) like Roger's own.
- **Pixels.** Rasterization is Metal's (OWNER_DRAW.md section 12); no
  framebuffer capture of a face frame is compared.
- **The bone-slot stack.** Fixed in chain step AIMCAP (2026-10-02,
  AIM_FIRE.md section 11.2): 0015C420's 21 pops of the player's node
  records (em_player_record_pose's storage at 0x7D5840..) now come off the
  shared 001AF710 stack, so every later pop, the face slots included,
  returns the original's address. Before, every later pop returned an
  address 21 slots lower than the original's (route 08 snapshot: free count
  1063 against 1042, cursor 0x7D47A4 against 0x7D47F8) and the first 21
  pops aliased the player's node addresses. The face units do not carry the
  slot address (their REF names the face resource), so their drawn bytes
  did not change. Chain step DAMAGE (2026-10-02) depends on it: the
  flame's burn node pops five slots from the same stack (DAMAGE.md section 3).

## 6. Binding (done 2026-09-28)

**Build.** `src/game/em_face_attach.c` and `src/game/em_face_slot.c` are in
the app (COMMON). Make targets: `test-face-attach-reference`,
`test-face-slot-reference`.

### 6.1 em_owner_draw_live.c (001CAA00's `w_001CB3C0`)

- `w_001CB3C0` runs `em_face_attach_001CB3C0(&L.face, record)` with:
  - one `EmAnimRest` (`L.rest`): channel = the module's channel-0 cursor
    (`L.channel`, count 1), scratch = `L.spr`, the region table = the
    call's regions, `w_001D88B0 = em_face_attach_w_001D88B0` with ctx =
    `L.face`;
  - `L.face.world`: draw = `L.draw.world`, light = `L.light` (the rig
    record D_00817BC0 and D_00275688 of 001D89D0), context_address =
    D_00275670's value, `d00250FB0` = the render context's .data view
    (render_context.emrc's D_00250F30 block, byte-checked against the ELF
    and every capture by tools/export_render_context.py), D_00275B40 = the
    owner's node array;
  - workers: `w_001D1F80` = the same cursor handover as 001CAA00's,
    `w_001D2910` = `em_rcl_001D2910`.
- `em_owner_draw_live_001CAA00_attached(bank, owner, rgb, record, regions,
  count)` takes the regions 001CB3C0 reads by address (the record, the slot
  arena, the face resource); `em_owner_draw_live_001CAA00` is the same with
  none (an owner with +0x90 != 0 then faults). Callers with an attachment:
  em_area11_roger (Roger) and em_player_draw_live (the player), both through
  `em_area11_roger_attachment_regions` (the record's bytes, the one
  001AF710 arena, the export's regions: Roger's face resource 0x88 and
  Dennis's 0x18).
- **The unit parse.** The appended bytes are parsed unit by unit
  (`em_object_unit_parse_one`): the owner's unit (absent when 001CA7B0
  culled the body), then 001CB3C0's face unit, which must end the bytes;
  anything else is refused. Both are kept for the flush in build order; the
  resolver maps the face REF (face + 0x40, qwc 0x163 per block) into the
  call's regions.
- **The log.** `EmOwnerDrawLiveLog` gains the face unit's bytes and a digest
  (B, the node rows, the weights); `bytes` is the owner unit's alone. The
  attached calls also leave a sample (`em_owner_draw_live_samples`: the
  inputs and every appended byte) for the level smoke.
- **Assets.** `tools/export_object_textures.py` adds Roger's model 0x47, his
  equipment's library model 0x6B and both face resources (Roger's 13 face
  TEX0 values; Dennis's 12 were already in the export): 346 textures,
  resident and identical in all 15 route captures.
  `tools/export_roger_banks.py` adds Dennis's face resource
  (extract/chunk03/f16_id18.bin at D_0028A490[0x18] = 0x011749C0, checked
  against RAM in 16 captures).

### 6.2 Roger (em_area11_roger, record 0x7A8830) and his equipment (0x7A8B20)

- Roger's +0x4C (em_roger's draw event) and the equipment's (001C5C90's
  jalr +0x4C) run `em_owner_draw_live_001CAA00_attached` over their records'
  owner views (+0x01, +0x02, +0x03, +0x09, +0x0C, +0x0D, +0x44, +0x80..+0x8F,
  +0x90, +0x94, +0x98, +0xB0 and the node records +0x110 names). Their
  models (0x47 at D_0028A490[0x47] = 0x01877740; library 0x6B) are added to
  a table-less bank from the export at their original addresses. 001C7420
  collapses node +0x94 = 7 of Roger's body; 001CB3C0 appends his face.
- The face kernel 001D0720 runs over the slot bytes
  (`em_opening_face_tick_slot`, shared with the player's slot); 001CB2C0
  uploads its weights with the face unit.
- **Retired:** the roger.emdl skinned draw, the em_face_model attach and the
  em_opening_face_position host-float morph of his face, the
  opening/equipment_6b.emdl draw at the equipment's bone 0, the draw list
  (`em_area11_roger_draw_count` / `_draw`) and em_render_frame's Roger chain
  entries.

### 6.3 The player's face slot (em_face_slot)

- `em_area11_interaction_host_player_face` gives the views: the live record
  D_008102B0, an EmRogerActor over the one 001AF710 stack and arena
  (em_area11_boxes), the shared 00122BB8.
- 001B81D0's 001CA700 / 001D06D0 (em_area_script, the script host's
  `w_001CA700` / `w_001D06D0`), 001B82D0 sub 4's 001CA770 (the script host's
  `w_001CA770` and the interaction host's RELEASE_SKELETON frame event),
  00183090's 001D0C70 (em_player_stage_live's `w_001D0C70` and the
  interaction runtime's cinematic worker) and 001FD950's 001D06E0 (the
  message host's face talk) run the translations on the record's +0x90 /
  +0x94. 001AF890 is live through 001CA770. The adapters accept any
  resource row; a call on a record address other than the player's has no
  record view in the script host and faults.
- The draw needs no change beyond the regions: em_player_draw_live's owner
  view copies +0x90 / +0x94, so 001CAA00 collapses node 7 and reaches
  001CB3C0 while the slot is held.
- **Oracle:** `tools/test_face_slot_reference.py` runs the ORIGINAL
  001B81D0 / 001D06E0 / 001CA770 / 001D0C70 sequence over playable_ee.bin
  (400 frames) against em_face_slot over a copy: +0x90, +0x94, the slot's
  0xD0 bytes, D_00275BCC, D_00275BD0, the stack word and the RNG draws equal
  after every event; plus the original call sites (00183090, 001FD950) and
  three fail-stop checks.
- **Retired:** em_player_face_host.{h,c} (its alternate mesh with the face
  attached, attach / detach / talk / tick / record), em_face_model.{h,c}
  (used only by it and Roger's morph), the interaction host's `world.face`,
  `em_area11_interaction_host_face_attach` / `_player_record` /
  `_face_state`, the RELEASE_SKELETON detach stand-in,
  `tests/player_face_host_test.c` and `tools/test_player_face_host.py`
  (superseded by test_face_slot_reference, rule 4; the fixture
  tests/area11_interaction_host_test.c now drives the slot), and the
  player-only / row-0x18-only refusals of the script host's adapters.

### 6.4 Census rows this changed (FIRST_LEVEL_CENSUS.md section 1.40)

| Row | Before | Now |
|---|---|---|
| 001CB3C0 | missing | live (em_face_attach through em_owner_draw_live) |
| 001D3E40, 001D3F50 | boundary (GS/VIF list) | live, moved to section 3.16 under the render-range rule |
| 001C7900, 001CB2C0 | verified-unbound | live (inside 001CB3C0) |
| 001D88B0, 001D8690 | verified-unbound | live (001CB3C0's 001C7900 -> em_face_attach_w_001D88B0 -> em_actor_light_001D8690) |
| 001CA770, 001AF890 | verified-unbound | live (the player's face slot) |
| 001CA700, 001D06D0, 001D06E0, 001D0C70 | live | evidence: em_face_slot, test_face_slot_reference |
| 001D2090, 001D2910, 001D2710 | live | no change; em_face_attach now calls them (no second copy) |

## 7. Live proof (the level smoke; LEVEL_SMOKE.md "Face attachments")

`tools/level_smoke_face.py` (check_face) over the full route:

- **Every attached call appends its face unit:** 15,328 attached 001CAA00
  calls to Roger (10,812: every frame his +0x4C ran) and the player (4,516:
  the frames a script held its face), each with one 0x190-byte face unit
  in the owner log.
- **Sampled re-execution:** the ORIGINAL 001CAA00 (001CA990's body unit,
  then 001CB3C0 and its callees) runs over route 14's RAM with the port's
  inputs of the call patched in (the record, its node records' +0x90
  matrices, the face slot, the tick's point-light pool, D_00810610, the cull
  planes, the view-projection, context +0x0C / +0x9C, the rig record and
  D_00275688, the area bytes): every byte the original writes equals the
  port's appended byte. Full mode: all 78 samples (Roger 55, the player 23;
  body culled and drawn, both units), quick mode 4 per record.
- **Against the captures** (check_owner_units): Roger and his equipment are
  compared like the player (movers whose clip phase follows the area load):
  in every aligned snapshot 08, 10..14 their face unit's length equals the
  original's, and in route 14 (camera exact) both are at the snapshot's
  point and pose and are compared in full (unit bytes, clip pass, B, the
  lighting and position rows).
- The captured face units of routes 10, 14 and the opening are rebuilt byte
  for byte offline (section 4).
