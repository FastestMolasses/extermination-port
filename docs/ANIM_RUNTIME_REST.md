# Animation runtime, the rest (census lane L33)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "anim-runtime-rest" (build lane `b7-anim-runtime-rest`). Scope: the 22
functions of census lane **L33-anim-runtime-rest**
(FIRST_LEVEL_CENSUS.md section 5). Six were missing, one was unverified and
fifteen were verified-unbound.

Code: `src/game/em_anim_runtime_rest.c/.h`. Oracle:
`tools/test_anim_runtime_rest_reference.py`. **The module is built and
tested but not wired.** Section 4 lists what the coordinator binds.

## 0. What already existed (checked first)

- `em_pose_host_workers.c` already translates and verifies (oracle
  `test_pose_host_workers_reference.py`, which executes each one unhooked)
  001C6120, 001C61D0, 001C63E0, 001C68C0, 001C6960, 001C8480, 001C8710,
  001C8F10, 001C90D0, 001C92C0 and 001C9940, and binds 001C94B0 to
  `em_owner_services_build_trs_matrix`. It also exports the verified leaves
  quat_nlerp (001CA0A0, `em_pose_host_001CA0A0`) and quat_to_mat3 (001CA1C0,
  `em_pose_host_001CA1C0`). This lane reuses those two leaves in 001C9D50 and
  does not translate anything twice.
- `em_owner_services_original.c` (COMMON) translates 001C7420 (the
  many-bone sibling of 001C7900) and 001C6150 (inlined in its allocators). It
  owns the display-list channel type (`EmOwnerServicesChannel`) and the SPR
  matrices type (`EmOwnerServicesScratch`: 0x70003400 / 3440 / 3480 / 3AC0).
  001C7900 uses exactly those words, so this module takes the same types.
  One channel array and one scratch struct then serve both uploaders.
- `em_cinematic_camera.c` (COMMON) translates 001C7C00
  (`test_roger_cinematic_reference.py` executes 0x1C7C00).
- `em_sdk_math_original.c` (COMMON) translates the SDK sqrtf 0011E748. This
  module adapts it (`em_anim_rest_sqrt_0011E748`).
- Nothing in the port translated 001C7900, 001C9D50, 001C9E40, 001CAAC0,
  001CACB0 or 001CB2C0 (grep for every spelling of each address).
  `test_player_pose_reference.py` names 001C9D50 only as a hook address.
- 001CB5B0: `em_status_models.c` `w_001CB5B0` (live) **does not write
  D_00275B40**. It only checks that the port's own "current record" exists.
  The owner-services, status-scene and Roger lanes have a worker slot for it,
  but none of them translates it.

## 1. Per function: status before -> after

"Before" is the census row. "After" is the status this lane's work
supports. The coordinator's next classification pass decides the final
value.

| Address | Decomp | Before | After | Module |
|---|---|---|---|---|
| 001C9E40 | AW | missing | verified-unbound | em_anim_runtime_rest |
| 001C9D50 | BM | missing | verified-unbound | em_anim_runtime_rest |
| 001C7900 | NM | missing | verified-unbound | em_anim_runtime_rest |
| 001CB2C0 | NM | missing | verified-unbound | em_anim_runtime_rest |
| 001CAAC0 | NM | missing | verified-unbound | em_anim_runtime_rest |
| 001CACB0 | BM | missing | verified-unbound | em_anim_runtime_rest |
| 001CB5B0 | BM | unverified | verified-unbound (the live `em_status_models.c` copy stays unverified; it does not write the word) | em_anim_runtime_rest |
| 001C6120, 001C61D0, 001C63E0, 001C68C0, 001C6960, 001C8480, 001C8710, 001C8F10, 001C90D0, 001C92C0, 001C9940 | BM / AW / NM | verified-unbound | unchanged (binding notes, section 4) | em_pose_host_workers |
| 001C94B0 build_trs_matrix | AI | verified-unbound | unchanged | em_owner_services_original (bound by em_pose_host_workers) |
| 001C6150 | BM | verified-unbound | unchanged | em_owner_services_original (inlined), em_status_models / em_roger_actor_original worker slots |
| 001C7420 | NM | verified-unbound | unchanged | em_owner_services_original |
| 001C7C00 | NM | verified-unbound | unchanged. Critic note 7.2 already corrects it to **live for S2** (em_render_frame.c -> em_opening_runtime_camera). It is unbound only for Roger's encounter (em_cinematic_playback.c is not in COMMON). | em_cinematic_camera |

## 2. What the originals do

Offsets are into the record named. Matrices are row-major, 16 words, row 3
the translation. "Channel" is the display-list cursor word at
D_00275670 + 0x10 + 4 * chan.

| Original | What it does |
|---|---|
| 001C9E40(q, m) (asm words) | trace = m22 + (m00 + m11). If trace > 0: s = sqrtf(1 + trace) (SDK 0011E748), q.w = 0.5·s, k = 0.5 / s, q.x = k·(m21 − m12), q.y = k·(m02 − m20), q.z = k·(m10 − m01). Otherwise the largest diagonal word picks the branch (m11 only if strictly greater than m00, then m22 only if strictly greater than that). For branch i: s = sqrtf(1 + ((m_ii − m_jj) − m_kk)), q_i = 0.5·s, and the other three are k times the pair sums or differences of the off-diagonal words. The stores and the word reads follow the original order. The sqrt argument is never negative: the branch choice keeps it at 1 plus a non-negative sum (section 5 has the sweep). |
| 001C9D50(out, a, b, blend) (BM) | w = 1 − blend. 001C9E40 turns a into 0x700034C0 and b into 0x700034D0. quat_nlerp(0x700034E0, 34C0, 34D0, blend). quat_to_mat3(out, 34E0, 34C0): the translation row it copies is the **first quaternion's** x, y, z (products staged at 0x70003760..). Then out row 3 xyz = a·w + b·blend through the COP1 accumulator (a product into ACC, then a multiply-add). a and b are read after out was written, so an out that aliases a sees the new words. |
| 001C7900(m, token, vuaddr, chan) (NM, .s followed) | The cursor on entry is the return value. It calls 001D88B0(m + 0x30, 0x70003400, 0x70003440, token). Then, at the cursor as re-read after that call: a qwc-5 packet (quadword +0x10 = 0, then FLUSH, STCYCL 1/1, UNPACK V4-32 ×4 to vuaddr, then B's four rows), with the cursor advanced by 0x60. Then a qwc-9 packet (quadword +0x10 = 0, STCYCL 1/1, UNPACK V4-32 ×8 to VU address 0): rows m × VP (0x70003AC0), then 0x70003480/90/A0 = m rows 0..2 normalised (w = 0), 0x700034B0 = m row 3 raw, and rows (3480..34B0) × A (0x70003400). The cursor is advanced by 0xA0. The DMA tag writes are byte +3 = 0x10, word +4 = 0 and halfword +0 = qwc. Bytes +2 and +8..+F are not written. |
| 001CB2C0(owner, vuaddr, chan) (NM, .s followed) | base = *(owner + 0x90). A qwc-3 packet: +0x10 = +0x14 = 0, STCYCL 4/4, UNPACK V4-32 ×2 to vuaddr, then the quadwords at base + 0x40 and base + 0x50, copied by 00102948 (its loads ignore the low four address bits). The cursor is advanced by 0x40. |
| 001CAAC0(position, payload) (NM, .s followed) | The quadword at position & ~15 (00102948) is projected by VP (0x70003AC0). Row 3 is weighted by the constant 1.0, not by p.w. Q = 1 / c.w (VU divide (3,3)), and c.xyz is multiplied by Q. The four lanes are converted to 12.4 fixed point. Only z is read back: key = z, then negative → 0xFFB000, then below 0x1000 → 0x1000. It calls 001CB760(0x007635C0, key, payload) and returns key. |
| 001CACB0(owner) (BM) | A tail call: 001CABA0(owner, *(owner + 0x44)). |
| 001CB5B0 anim_bone_array_setup (BM) | D_00275B40 = D_00275B48 + 0x110. The callers' argument is not read. |

## 3. The translation

- Every COP1 and VU0 instruction goes through `em_ee_float.h` under its real
  form. The forms used are MULABC xyzw/x, MADDABC xyzw/y and /z, MADDBC
  xyzw/w, MUL xyz, ADDBC x/y and x/z, ADDQ x, SUB xyzw (same register),
  MULQ xyz, VDIV (3,0) and (3,3), VSQRT and VFTOI4. All of them are in the
  measured table, so the `UNMEASURED` fault cannot happen.
- **Fail-stop.** Before its first write and before its first worker call,
  every entry checks every view, worker, channel index, channel room and EE
  mapping it will reach. If one is missing it latches a fault
  (`EmAnimRest.fault`: address and code) and returns −1 with nothing
  written. 001C7900 checks the channel room again after the 001D88B0 call,
  because that call may have appended to the same channel. A worker failure
  latches a fault, and the writes already made stay in the original order.
  For example, a sqrt failure in the second 001C9E40 of 001C9D50 leaves
  0x700034C0 written. A latched fault blocks every later call until the
  caller clears it.
- **Data by type.** The SPR words 3400/3440/3480/3AC0 and the channels are
  the owner-services types. 0x700034C0/D0/E0 and 0x70003760 are word
  pointers (3760 is the same 11 words as `EmPoseGlobals.spad3760`). The EE
  reads (owner +0x44 / +0x90, the attachment quadwords, the position
  quadword) go through `EmPoseRegion` ranges.
- **Dead lanes.** 001CAAC0 also clamps the projected w lane by the fog quad
  at D_00275670 + 0xA0 and converts x, y and w. Those lanes land only in its
  own stack frame and are never read, so the translation does not compute
  them and does not read the fog quad (section 6).

## 4. Binding (for the coordinator)

One `EmAnimRest` for the game.

**World** (`EmAnimRestWorld`):

| Field | Bind to |
|---|---|
| `channel`, `channel_count` | The same `EmOwnerServicesChannel` array as `EmOwnerServicesWorld.channel` (D_00275670 + 0x10 + 4·chan; 001CB760 uses channel 2's word +0x18 itself). |
| `scratch` | The same `EmOwnerServicesScratch` as `EmOwnerServicesWorld.scratch` (001C7420 and 001C7900 both write A/B/C and read VP). |
| `spad3760` | `EmPoseGlobals.spad3760` of the pose host (the same 11 words). |
| `spad34C0/D0/E0` | The coordinator's scratchpad words (4 each). No other port module names them. 001C9D50 writes each one before it reads it. |
| `d275B40` | The coordinator's live D_00275B40 word, the same storage that `EmPoseActorView.d275B40` points at. |
| `d275B48` | The coordinator's D_00275B48 (the record being run; 001CB590 writes it). |
| `region[]` | EE ranges for the owner records (+0x44, +0x90), the attachment records (+0x40..+0x5F) and the 001CAAC0 positions. |

**Workers** (`EmAnimRestWorkers`):

| Slot | Bind to |
|---|---|
| `w_0011E748`, `sqrt_ctx` | `em_anim_rest_sqrt_0011E748` with the shared `EmSdkMathContext` (SDK_MATH_ORIGINAL.md section 7). The SDK doc's gate on sqrt sites is satisfied for this site: the argument is 1 plus a non-negative sum on every path, and the sweep (section 5) found no negative argument. |
| `w_001D88B0` | The L32 lane's 001D88B0 (`em_lighting.c` "face lighting mode" today, census **unverified**). Until it is verified, bind nothing: 001C7900 then faults. |
| `w_001CB760` | No translation exists (see census correction 1). Until L39 translates it, bind nothing: 001CAAC0 then faults. |
| `w_001CABA0` | The renderer boundary (001CABA0 builds GS packets; the census counts it as boundary). |

**Live call sites and stand-ins replaced:**

- 001C9D50 has one route caller, anim_matrix_player 0017B660 (lane L12,
  **stand-in** today: the legacy gait display). When L12 translates
  0017B660, its 001C9D50 slot = `em_anim_rest_001C9D50`, with `out` = node i
  +0x90, `a` / `b` = D_00288D40 / D_00287F40 + 0x40·i, and `blend` = player
  +0x208 raw bits. It replaces nothing live by itself.
- 001C7900 and 001CB2C0 have one route caller, 001CB3C0 (Roger's attachment
  draw, reached through 001CAA00 when owner +0x90 is nonzero; the route's
  only such owner is Roger 0x7A8830, attachment 0x7D9530). 001CB3C0 is
  `EmOwnerServicesWorkers.w_001CB3C0` and has no translation (lane L35). That
  translation calls `em_anim_rest_001C7900(r, buf, owner + 0x80, 0x3F5, 0)`
  and `em_anim_rest_001CB2C0(r, owner, 0x3F3, 0)`. What reaches the GPU
  today in their place is the native renderer (em_render_frame.c).
- 001CAAC0 has two route callers: 001CABA0 (boundary) and the effect manager
  001F6210 (lane L26, missing). Bind it into L26's translation.
- 001CACB0 is the draw method +0x4C of the indicator children (the 001CA5F0
  table entry 2; the owners 0x7ACBC0.., see ORIGINAL_FRAME_ORDER.md rows 39–48).
  The coordinator's method dispatch for 0x001CACB0 calls
  `em_anim_rest_001CACB0(r, owner)`. Today em_status_models.c only stores the
  address, and the pickup lights are drawn natively (ACTOR_LIGHTING.md).
- 001CB5B0: replace `em_status_models.c` `w_001CB5B0` (and bind the
  `w_anim_bone_array_setup` / `w_001CB5B0` slots of em_owner_services_original,
  em_status_scene_original and em_roger_actor_original) with a thin adapter
  that calls `em_anim_rest_001CB5B0` on the one `EmAnimRest`. The slots have
  different context signatures (`uint8_t` / `uint32_t count`, unused by the
  original), so the adapter only drops the count.

**Verified-unbound rows (binding only, no new code):**

| Original | Translation | Binding |
|---|---|---|
| 001C6120 | `em_pose_host_001C6120` | `EmStatusSceneWorkers.w_001C6120` / `EmOwnerServicesWorkers.w_001C6120` today go to `em_status_models.c` (a bank-restricted lookup of the port's exported models). The faithful lookup is the pose-host routine over the mapped bank. |
| 001C6150 | inlined in `em_owner_services_001B0EA0` / `_001B0DC0` | `em_status_models.c` `w_001C6150` reads the model byte +8 the same way. No change needed beyond mapping the model record. |
| 001C61D0 | `em_pose_host_001C61D0` (`_clip_frames` adapters) | Every state lane's `clip_frames` slot (POSE_HOST_WORKERS.md section 3). |
| 001C63E0 bone_init_default_2 | `em_pose_host_001C63E0` | Needed by the player's stage workers (`bone_init`) and by Roger (008237E0 passes clip 8) and the status models. **The live `em_status_models.c` `w_001C63E0` is a stand-in** (it calls `em_player_pose_init` on the exported bank and fills `EmOwnerBone` fields). The census row calls it verified-unbound only because the translation exists. The pose-host routine works on raw 0xD0-byte node records (it also writes the channel words +0..+6B, which `EmOwnerBone` does not model), so binding it needs raw node storage for Roger's and the menu models' bones, not an adapter over `EmOwnerBone`. That storage decision is the coordinator's. |
| 001C68C0 / 001C6960 / 001C9940 | `em_pose_host_001C68C0` / `_001C6960` / `_001C9940` | POSE_HOST_WORKERS.md section 3 (the `skeleton` slots; em_door.h names 001C68C0 in comments only). |
| 001C8480, 001C8710, 001C8F10, 001C90D0, 001C92C0 | em_pose_host_workers (clip resolve, direct sample and the three key walks) | The stage adapters `em_pose_host_stage_clip_resolve` / `_8710` (POSE_HOST_WORKERS.md section 3). |
| 001C94B0 | `em_owner_services_build_trs_matrix` | Through `em_pose_host_build_trs_matrix`. |
| 001C7420 | `em_owner_services_001C7420` | Reached by `em_owner_services_001CA990`. It shares channel and scratch storage with 001C7900 (above). |
| 001C7C00 | `em_cinematic_camera_sample` | Live for S2. For beat 14, em_cinematic_playback.c must join COMMON (census critic note 7.2). |

**Sources needed when bound:** `src/game/em_anim_runtime_rest.c`, plus
the four pose-host sources that are not yet in COMMON
(`em_pose_host_workers.c`, `em_player_stage_workers.c`,
`em_player_reaction.c`, `em_stream_lanes_original.c`; the pose-host lane
lists the same four). `em_owner_services_original.c`,
`em_sdk_math_original.c` and `em_player_floor.c` are already in COMMON. A
trial link on 2026-09-23 (the private lane build command with those five
sources appended) built with zero warnings and no duplicate symbols. No
existing file needs an edit.

## 5. Verification

`python3 tools/test_anim_runtime_rest_reference.py`. Suggested target:
`make test-anim-runtime-rest-reference`.

- **Interpreter.** The shared EE (test_player_slide_reference.py) with the
  COP1/VU0 float model of `test_coll_move_reference.FloatEE`
  (`tools/ee_float_model.py`), plus the address recording of
  test_pose_host_workers_reference.py. The test file adds only the two MMI
  word-interleave forms that 001D88B0's callees execute.
- **Executed unhooked:** 001C9E40 (with 0011E748 and its kernel), 001C9D50
  (with quat_nlerp and quat_to_mat3), 001C7900 (with 001D88B0 and
  everything below it: the full mode-0 lighting setup, which is the mode in
  every image), 001CB2C0, 001CAAC0 (with 00102948 and 001CB760), 001CACB0
  and 001CB5B0. The route case also runs 001CB3C0 on Roger's record.
  **Hooked, arguments recorded only:** 001CABA0, plus 001D1F80 and 001D3F50
  inside 001CB3C0 (renderer boundaries).
- **Native workers.** `w_0011E748` is the production SDK translation
  (tables from the user's ELF). `w_001D88B0` and `w_001CB760` are the
  **original routines executed over the native memory** by a second
  interpreter, with the channel cursors and SPR matrices synchronised in and
  out. The native therefore gets exactly what the original callee does with
  the native's arguments. `w_001CABA0` records its arguments.
- **Comparison.** After every case: the whole 32 MB native RAM (channel
  cursors written back) equals the original RAM byte for byte, the
  scratchpad equals the original scratchpad, the worker-call logs are equal
  (the 16 position bytes and a1..a3 for 001D88B0; table, key and payload
  for 001CB760; owner and model for 001CABA0), and every return value is
  equal (001C7900's entry cursor, 001CAAC0's key).
- **Cases.** On every captured image (playable + 06_hill_slide +
  14_roger_encounter in quick mode, all 16 in full mode):
  - 001C9D50 as anim_matrix_player calls it (node i +0x90 from D_00288D40 /
    D_00287F40 at the player's +0x208), plus blends 0, 1, 0.25, 1.5 and −0.5;
  - 001C9E40 over the node world matrices;
  - **Roger's 001CB3C0** (D_00275B40 = Roger + 0x110), which captures the
    route's exact 001C7900 stack matrix and arguments and its 001CB2C0 call;
  - 001C7900 with player node matrices; 001CB2C0 on Roger;
  - 001CAAC0 at the player position and at the node positions;
  - 001CACB0 on every indicator owner (8 or 9 per image);
  - 001CB5B0.

  Synthetic cases (06_hill_slide image, 25 quick / 600 full): random
  rotations with scale, 001C9E40's branch boundaries (each diagonal word
  largest, ties, trace = 0), raw bit patterns, a negative quaternion dot, an
  out that aliases a, random 001C7900 channels and VU addresses, unaligned
  attachment and position addresses (the quadword masking), and 001CAAC0
  points found on the captured view-projection that hit both key clamps.
- **Coverage.** Every reachable instruction of the seven functions runs. The
  one exception is 001C9E40's fall-through after the three branch tests
  (0x1C9F34 and its delay slot). The branch index is 0, 1 or 2 by
  construction, so that path cannot run.
- **Sqrt domain.** 4,000 quick / 200,000 full matrices of random raw words
  and special values (±0, ±MAX, exponent-255 words, denormals) through the
  native 001C9E40 with the production SDK sqrt. None faulted, so no negative
  argument reached 0011E748.
- **Fail-stop.** 24 checks: null scratch, 001D88B0, channel or VU-matrix
  views; channel index −1 and 4; short channel room (001C7900 at 0xFF bytes,
  001CB2C0 at 0x3F); a failing 001D88B0; unmapped owner, attachment and
  position; null 001CB760, 001CABA0, sqrt, 34E0 or 3760; null D_00275B40 or
  D_00275B48; and the second sqrt failing inside 001C9D50 (only 0x700034C0
  written, as in the original). Each check requires −1, a latched fault,
  nothing written (or exactly the original's partial write), and a blocked
  second call.
- **Mutation check** (run once while writing, not part of the suite): five
  injected one-token bugs (a STCYCL constant, the 0x1000 clamp, the blend
  factor, one quaternion difference and 001C7900's translation row) were
  each caught by several cases.

Results (2026-09-23): quick passes in 0.4 s: 15 captured-state cases over 3
images, 25 of 60 synthetic cases, 211 original calls, 87 worker calls
compared, 4,000 sqrt-domain matrices and 24 fail-stop checks. Full
(`EM_TEST_FULL=1`) passes in 6.0 s: 80 captured-state cases over 16 images,
600 synthetic cases, 4,310 original calls, 1,266 worker calls compared,
200,000 sqrt-domain matrices and 24 fail-stop checks.

## 6. Limits

- **No capture comparison.** The route captures are taken at frame
  boundaries. A check was tried and dropped: 001C9D50 over 06_hill_slide's
  captured pose buffers does not reproduce the captured node matrices,
  because the player tail re-evaluates the skeleton later in the frame
  (POSE_HOST_WORKERS.md's capture check reproduces those matrices from the
  channels). So the evidence here is the executed-instruction oracle over
  captured inputs, not a captured output.
- **VU registers.** 001CAAC0 leaves values in vf1, vf2, vf23 and
  vf28–31, and 001C7900 leaves values in vf4–vf15. The port does not model
  VU registers across calls. Whether any later original code on the route
  reads those registers without reloading them first has **not been
  established**. The oracle compares memory only, not the VU register file
  after the return.
- **Alignment contract.** 001C7900 and 001CB2C0 store quadwords at the
  channel cursor. The original's quadword stores ignore the low four address
  bits and the host stores do not. The two agree for 16-byte-aligned
  cursors. All four channel cursors are aligned in all 16 captured images. The EE-address reads (attachment, position)
  are masked exactly as the original masks them.
- **Unbound workers.** 001D88B0 (L32, unverified) and 001CB760 (L39, not
  translated) have no bindable translation yet. Until they exist, the
  routines that reach them must stay unwired. They fault if called.
- **Route coverage.** As the census notes: one hit per function per label,
  and nothing after Roger's encounter releases control.

## 7. Census corrections found by this lane

1. **001CB760** is listed as verified-unbound (em_head_sprite_original). It
   is only a worker slot there (`w_001CB760`), and `test_head_sprite_reference.py`
   hooks it (it records the call). Nothing translates it. Should be
   **missing** (lane L39).
2. **001C63E0**: the live `em_status_models.c` `w_001C63E0` is a
   **stand-in** (`em_player_pose_init` over the exported bank), not the
   translation. The translation (`em_pose_host_001C63E0`) is verified and
   unbound.
3. **001CB5B0**: the live copy in em_status_models.c does not write
   D_00275B40, so the row's "unverified" status understates the gap. The
   verified translation is now `em_anim_rest_001CB5B0`.
4. **001CB3C0** is listed as verified-unbound under em_owner_services_original.
   It is only a worker slot there (`w_001CB3C0`), and
   `test_owner_services_reference.py` hooks it. Nothing translates it
   (lane L35).
