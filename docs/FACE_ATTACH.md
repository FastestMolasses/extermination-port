# Face attachment draw (001CB3C0) — lane FACE_ATTACH

Status (2026-09-27, lane b15 FACE_ATTACH): **translated and verified, not
bound.** The +0x90 attachment draw of the 001CAA00 owners (Roger's face, and
the player's Dennis face while a script holds his face slot) now has a native
translation that builds the original face unit byte for byte. The unit is the
one `em_object_unit` already parses and runs through the face-morph program
(OWNER_DRAW.md section 8 F). Nothing live calls it yet; section 6 is the
binding recipe.

| File | Role |
|---|---|
| `src/game/em_face_attach.{h,c}` | 001CB3C0, 001D3F50, 001D3E40, and inline copies of vif_append_ref_tag (001D2090) and 001D2910(0) (both already live elsewhere, section 2), plus `em_face_attach_w_001D88B0`, the adapter that runs the existing 001D88B0 translation as the EmAnimRest worker |
| `tools/test_face_attach_reference.py` | The original-instruction oracle (section 4) |
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
| 001C7900, 001CB2C0 | `em_anim_rest_001C7900` / `_001CB2C0` (ANIM_RUNTIME_REST.md; verified-unbound until now) |
| 001D88B0, 001D8C30 | `em_frh_001D88B0` / `em_frh_001D8C30` (em_frame_render_heads), run by `em_face_attach_w_001D88B0` over EmFrh views of the light world, SPR A / B and the token bytes |
| 001D8130, 001D8340, 001D8690 | `em_actor_light_001D8130` / `_001D8340` (owner NULL) / `_001D8690` (the same rig record as 001D89D0) |
| 001D1F80 | a worker; the test binds `em_load_veil_particles_001D1F80` |

**Duplicates (verified, but second copies).**

- **vif_append_ref_tag (001D2090) and the tag writer** are also static in
  em_owner_draw_original.c, where they are live through em_owner_draw_live.
  That file is tracked, so this lane could not export them.
- **001D2910(0) / 001D2710(0)** are written inline (context +0x0C bit 0),
  although exported, live translations exist: `em_render_context_001D2910` /
  `_001D2710` (em_render_context.h, through em_render_context_live). They are
  not called because this module holds only the owner-draw world's `ctx_0C`
  view, not an EmRenderContext; calling them needs the render context the
  binding already has (em_render_context_live's).

Reduce both copies when binding: call the exported vif_append_ref_tag once
em_owner_draw_original exports it, and `em_render_context_001D2910(ctx, 0,
&r)` with the live render context.

## 3. The native API

- `EmFaceAttachWorld` holds views only; the module keeps no copies:
  - **anim**: the one EmAnimRest. Its region table serves every EE-address
    read: record +0x80..+0x97, slot +0x40..+0x63 and face +0x04. Its channel
    and scratch are those of 001C7420 / 001CA940.
  - **draw**: the owner-draw world (context +0x0C / +0x9C / +0x50,
    D_00275674). Its channel must be the anim rest's.
  - **light**: the EmActorLight of 001D89D0 (the rig record, D_00275688,
    context +0x246C / +0x2380).
  - **context_address**: *D_00275670.
  - **d00250FB0**: 3 words.
  - **d00275B40** and its count: the walk's node array, the same as
    `EmOwnerServicesWorld.d00275B40`.
- `EmFaceAttachWorkers`:
  - `w_001D1F80`;
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
- **23 fail-stop checks.** The node array given to the native has a
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
| default | 21 | 30 of a pool of 42 | 645 | 3 | 2.1–5.2 s | 3.9–6.6 s |
| `EM_TEST_FULL=1` | 21 | 150 | 2,193 | 40 | 9.0–9.5 s | 32–33 s |

Timings are the lane's and the review's reruns (2026-09-27); the default run
is slower when the private dylib is rebuilt.

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

- **Not bound.** Section 6 lists what binding needs.
- **The face weights.** Slot +0x40..+0x5F are consumed as they are. Their
  live writer for Roger is em_opening_face_tick over the slot bytes (the
  001D0720 face kernel). Its header says host float rounding was not
  asserted bit-identical. Until that kernel is verified, the unit is exact
  only for exact weights.
- **The draw-time view.** A face unit, like the player's 001D89D0 camera
  fill, must be built while D_00810610 still holds the frame's draw-time
  view (section 4). Whether the port's frame order holds that view at the
  call has not been checked; check it when binding.
- **VU registers.** 001C7900 leaves values in VU0 registers
  (ANIM_RUNTIME_REST.md 6); not modelled.
- **The duplicates** (section 2): the tag writer / vif_append_ref_tag and
  the inline 001D2910(0) / 001D2710(0).
- **001D3E40 on channels other than 0** is not verified (section 3).
- **No ASan fixture.** The module is exercised from Python through ctypes;
  the trial link of the app with it added builds with zero warnings.

## 6. Binding (for the chain)

**Sources.** `src/game/em_face_attach.c` joins the app. Everything it calls
is already in the app build:

- em_anim_runtime_rest;
- em_frame_render_heads;
- em_actor_light_001D89D0;
- em_owner_draw_original;
- em_owner_services_original;
- em_load_veil_particles.

A trial link on 2026-09-27 (the private lane build command with this file
appended) had no warnings and no duplicate symbols. Suggested make target:
`test-face-attach-reference` → `python3 tools/test_face_attach_reference.py`.

### 6.1 em_owner_draw_live.c (the only caller: 001CAA00's `w_001CB3C0`)

1. Replace the faulting `w_001CB3C0` stub with an adapter that calls
   `em_face_attach_001CB3C0(&L.face, record)`. `record` is the original
   record address `em_owner_draw_live_001CAA00` already receives; keep it in
   L for the adapter.
2. Set up one `EmAnimRest` in L:
   - `channel = &L.channel` (count 1) and `scratch = &L.spr`;
   - `workers.w_001D88B0 = em_face_attach_w_001D88B0` with
     `workers.ctx = &L.face`.
3. Give that EmAnimRest these regions, all mapped read-only onto canonical
   bytes:
   - the owner record's [+0x80, +0x98): Roger's record image in
     em_area11_roger, or the player's record;
   - the face slot's 0xD0 bytes in the shared 001AF710 slot arena
     (em_area11_boxes / em_roger_actor_original's slot storage);
   - the face resource's header and blocks.
4. Set up `L.face.world`:
   - `draw = &L.draw.world`, `light = &L.light` (the same rig record and
     D_00275688 as 001D89D0);
   - `context_address = CTX`;
   - `d00250FB0`: 3 words that are not yet in any port export. Add them to
     an exporter (export_render_context or export_roger_banks) with a
     byte-for-byte check against the ELF; the test already asserts the
     captures equal the ELF there;
   - `d00275B40` / count: `s->world.d00275B40` / `d00275B40_count` of the
     same call (owner->bone);
   - `workers.w_001D1F80`: the module's existing `w_001D1F80` (the
     cursor handover).
5. **The unit parse.** After `em_owner_services_001CAA00`, the used bytes
   are the owner unit followed by the face unit. **When 001CA7B0 culled the
   body, only the face unit is there** (used = 0x190 / 0x1A0).
   - Today's `if (!used) return 0` and the single `em_object_unit_parse`
     must become: parse the owner unit when the cull flags were ≥ 0, then
     parse the face unit (`em_object_unit_parse_one` on the rest).
   - Keep both for the flush, in build order.
   - The resolver must map face + 0x40 .. + 0x40 + 16·qwc to the face
     resource.
6. **Assets:**
   - **Roger's face resource** (D_0028A490[0x88] = 0x018C8740) is in
     `assets/scene_snow/roger/resources.emrs` (export_roger_banks: "the face
     resource 0x88 at +0x86000").
   - **Dennis's** (D_0028A490[0x18] = 0x011749C0) is in no raw export yet.
     `opening/player_face.emfm` is a different format. Add a region to an
     exporter, checked against RAM in the captures.
   - **Textures:** Roger's face kicks 13 TEX0 values that
     `object_textures.emot` does not hold. export_object_textures.py must add
     the face resources' block TEX0s. Dennis's 12 are already exported
     (shared with the player model). Until the export holds Roger's,
     `em_gfx_object_unit` faults at his first face unit, which is correct
     fail-stop.

### 6.2 Roger (em_area11_roger, record 0x7A8830; +0x4C 001CAA00, +0x90 = his slot, +0x94 = 7)

- Replace "the port's actor draw of Roger's mesh (roger.emdl) with the face
  morph of the slot" with `em_owner_draw_live_001CAA00` over Roger's body
  model 0x47. That model is in resources.emrs; it needs an EmWorldModels
  entry (em_world_models_add at its address). The call uses his record's
  owner view with `attachment` = +0x90 and `collapsed_bone` = +0x94.
- 001C7420 already collapses node +0x94 (it keeps only that node's
  translation; OWNER_DRAW.md section 3), so the body unit needs no mesh
  edit under the face, as far as the original's own draw goes.
- **Retire** for Roger in AREA11:
  - the roger.emdl skinned draw;
  - the em_face_model attach / em_opening_face_position host-float morph
    used to draw his face.

  The face state and em_opening_face_tick stay (they write the weights
  001CB2C0 uploads).

### 6.3 The player: binding 001CA700 / 001CA770 on the player

Today the player's face is `em_player_face_host`'s own state. The
interaction host's `face_attach` / `em_player_face_host_detach` stand in for
001B81D0's 001CA700(player, D_0028A490[0x18], 7) + 001D06D0(player, 1), and
for 001B82D0 sub 4 / the release op's 001CA770(player). Binding the
originals needs:

1. **The face in a pool slot.**
   - 001CA700 pops a slot with 001AF780: the one D_00275BD0 stack /
     D_00275BCC count shared with Roger and every node allocation, which
     refuses below 31 free.
   - It stores the player's +0x90 = slot and +0x94 = 7, slot +0x60 = the
     resource, and resets slot +0x70..+0xA7 (001D0690).
   - 001D06D0 then sets slot +0x81 = 1.
   - The translations exist as `em_roger_actor_001CA700` / `_001D06D0` /
     `_001CA770` / `_001AF780` / `_001AF890`. They are typed on
     EmRogerActorRecord (only the `face` / `face_bone` fields) and on the
     Roger actor's slot storage. The player needs the same over the player
     record's +0x90 / +0x94 and **the same slot arena**: either a generalised
     record view or an adapter with store-back.
   - This makes 001AF890 live through its original caller (C7 note).
2. **The face kernel on the slot.**
   - `em_area11_interaction_host_face_tick_001D0C70` (00183090's
     001D0C70 under 0x70003B8F == 2) must run 001D0720 over the slot bytes,
     as em_area11_roger does for Roger, instead of the face host's state.
   - 001FD950's 001D06E0(player, 0 / 1) must write slot +0x80 (and clear
     +0x90..+0xA7 on 0).
3. **The draw.**
   - 0015C160's +0x4C (`em_player_draw_live`) already calls
     `em_owner_draw_live_001CAA00` on the player's owner view, and that view
     already copies `attachment` / `collapsed_bone` from the record's +0x90 /
     +0x94 (em_player_draw_live.c). **No change is needed there**: once
     001CA700 writes a slot to +0x90, the draw reaches 001CB3C0.
   - With the face slot set, 001CAA00 reaches 001CB3C0 (6.1) and 001C7420
     collapses node 7.
   - Dennis's face resource export (6.1, step 6) is required.
4. **Retire:**
   - em_player_face_host (the alternate mesh with the face attached, its
     attach / detach / tick / record API);
   - the interaction host's `world.face`;
   - the RELEASE_SKELETON stand-in in em_area11_interaction_host.c;
   - area11_script_host's `w_001CA700` / `w_001CA770` player-only adapters
     (bind the translations);
   - the "001CA770 on an actor other than the player" / "row-0x18 only"
     refusals: the original takes any actor.
5. **Out of this lane:** the opening's player and face (design risk 2) stay
   on em_opening_actor until the opening's actors move onto their records.

### 6.4 Census rows this changes

Checked against FIRST_LEVEL_CENSUS.md on 2026-09-27:

| Row | Today | Change |
|---|---|---|
| 001CB3C0 | missing | missing → verified-unbound (em_face_attach); live when 6.1 is bound |
| 001D3E40, 001D3F50 | no row: in the section 4 "GS/VIF packet build / VU1 kick" boundary list | boundary → verified-unbound, moved out under the section 4 render-range rule ("a function in the render ranges that carries a translation is classified normally"); live when 6.1 is bound |
| 001D2090 vif_append_ref_tag | live (em_owner_draw_original through em_owner_draw_live) | **no change**; em_face_attach's copy is a duplicate (section 2) |
| 001D2910, 001D2710 | live (em_render_context through em_render_context_live) | **no change**; the inline 001D2910(0) is a duplicate (section 2) |
| 001C7900, 001CB2C0 | verified-unbound | no status change; now also exercised inside 001CB3C0 against the original; live when 6.1 is bound |
| 001D88B0 (em_frh) | verified-unbound | no status change; it now has a bindable adapter and an oracle through 001CB3C0; live when 6.1 is bound |

The boundary list's count (28) drops by two when 001D3E40 / 001D3F50 move
out.
