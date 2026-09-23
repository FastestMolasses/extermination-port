# Owner model services (shared layer)

Status: 2026-09-23 (fix round: arithmetic moved onto em_ee_float.h, raw-bit float ABI). Standalone translation and oracle, lane `owner-model-services`. **Not bound into the
frame.** The coordinator binds it; see "Binding" below.

These are the generic original routines that the AREA11 owners call for model binding, bones, placement,
publication, drawing and rumble. The truck, crates, drums, fan, Roger, elevator and pickups all reach them
through their worker tables.

Files:
- `src/game/em_owner_services_original.{h,c}`: the native translation.
- `tools/test_owner_services_reference.py`: the original-instruction oracle (`make test-owner-services-reference`).
- `tests/owner_services_test.c`: the native fail-stop contract (`make test-owner-services`).

## What is translated

Every routine below was read from the decomp C, or from the splat `.s` where the C is NEARMISS or `.word`.

| Original | Native | Notes |
|---|---|---|
| 001B0FD0 | `em_owner_services_001B0FD0` | If 001B0EA0 returns nonzero, returns 1. Otherwise runs bone_init_default_1, then +0x04 += 1 and returns 0. |
| 001B0EA0 (NEARMISS) | `_001B0EA0` | 001C6120(`*D_0028A59C`, +0x0D) → 001CA6E0 → +0x0C = model +0x08 (001C6150).<br>If `D_00275BCC` (a signed halfword) < +0x0C: +0x04 = 3, returns 1.<br>Otherwise +0x110[i] = 001AF780() for each bone (+0x0C is re-read on every pass), +0x09 = count, 001CB5B0(count), returns 0. |
| 001B1020 (asm) | `_001B1020` | Calls 001B0DC0(a0, a1, a2); if nonzero, returns 1. Otherwise +0x04 += 1. Then a2 == -1 → bone_init_default_1; any other a2 → bone_init_default_2(owner, (int16)a3). Returns 0. |
| 001B0DC0 | `_001B0DC0` | Same shape as 001B0EA0, with the bank `*D_0028A56C`. The **a1 register reaches 001C6120 as the model id**. The decomp comment "arg1 unused" is wrong on that point. When a2 != -1: +0x40 = `D_0028A490[a2]`. |
| 001C62C0 bone_init_default_1 (`.word`) | `_001C62C0` | Decoded from its words. For each bone i < +0x0C, with record r = model + `*(model+0xC)` + 0x50·i:<br>node +0x64 = r+0x04 (halfword);<br>+0x88/8A/8C = 0x1000;<br>+0x70..+0x84 = 0;<br>+0x00..+0x3F = r+0x10..+0x4F. |
| 001C6380 | `_001C6380` | build_trs_matrix(+0xD0, +0xB0, +0xC0, +0x60), then 001C9610(+0x110, +0x0C, +0xD0). |
| build_trs_matrix | `em_owner_services_build_trs_matrix` | Steps: identity; rotate X, then Y, then Z (00102B08/BB0/A60); rows 0..2 xyz × scale x, y, z respectively (MULBC xyz with broadcast lanes x, y, z); 00102918 translate. |
| 001C9610 (NEARMISS) | `_001C9610` | Per node:<br>SPR 0x70003400 = identity;<br>00102C58 with the Euler angles at +0x70 (Z, then Y, then X);<br>row 3 = +0x7C..+0x84;<br>rows 0..2 xyz × (float)(s16) scale · 2⁻¹² (EE int-to-float convert, EE multiply, then a VU xyz multiply by that value);<br>SPR = SPR × bind (+0x00);<br>+0x90 = SPR × (parent +0x90, or the root +0xD0 when +0x64 == -1). |
| 001029C0, 001029E8, 00102A60, 00102B08, 00102BB0, 00102C58, 00102918 | `em_owner_services_identity/rotate_x/_y/_z/euler/translate_*` | The SDK VU0 routines, lane by lane. The sine coefficients D_00241100 are the four words the oracle compares with the user's ELF at run time. |
| 00102958 copy_qw4 | `em_owner_services_copy_qw4_00102958` | Raw 64-byte copy. |
| 001B17A0 | `_001B17A0` | Applies only while D_00810CA5 == 6. The 001B1CE0 gate depends on the class (+0x02 & 0x1F):<br>2: (+0x03 == 1 and +0x0D bit 0) or +0x03 == 7;<br>8: +0x03 == 7;<br>0xA: +0x03 == 1;<br>7: +0x03 == 0 and +0x2E >= 0x2D.<br>Then +0x01 = 001B1630(+0xB0, +0xB4, +0xB8). When it is nonzero, 001B1B70 runs. Returns +0x01. |
| 001CAA00 | `_001CAA00` | Radius: model +0x20. When it is < 20.0 (EE compare), it is × 1.2 (0x3F99999A, EE multiply). With no model it is 20.0.<br>Position: +0xB0 when +0x98 == 0xFF, otherwise `D_00275B40[+0x98]` + 0xC0.<br>Then calls 001CA990, and 001CB3C0 when +0x90 != 0. |
| 001CA990 | `_001CA990` | r = 001CA7B0(pos, f12 = radius). The C prototype drops the float, but the `.s` passes f12 through.<br>When r >= 0: 001D8C20(0), 001C7420(owner, 0x3F5, 0), 001D1F80(0, 1, 0), 001CA940(r, +0x44). |
| 001C7420 (NEARMISS) | `_001C7420` | See the packet section below. |
| 001B1E20 | `_001B1E20` | a1 < 0 (the full 64-bit register is tested): 001B6250(&D_00810E40).<br>a1 > 0: 001B61C0(t[0], t[1], (int16)a1, 1).<br>a1 == 0: 001B61C0(t[0], t[1], t[2], 1).<br>t = D_0024D6F0 + 4·a0. |
| 001B5B70 | `_001B5B70` | Runs when D_00810E56 != 0. If the halfword D_00810E68 == 0: 001B6250(&D_00810E40). Otherwise it is decremented (a halfword store, so -32768 becomes 32767). |

**001C7420 packets.** The cursor is `D_00275670 + 0x10 + 4·chan`. The steps are:

1. **Light matrices.** 001D89D0(owner, 0x70003400 = A, 0x70003440 = B, owner + 0x80).
2. **Colour packet.** CNT qwc 5 (tag bytes +3 = 0x10, +4..7 = 0, +0..1 = qwc). It carries 0, FLUSH 0x11000000, STCYCL 0x01000101 and UNPACK 0x6C040000|vuaddr, then B.
3. **Clear B.** B is zeroed with a VU register minus itself (form SUB xyzw). That form clamps its operands, so the result is always +0.
4. **Node packets.** Chunks of up to 248 qw (0xF8). Each is a CNT qw chunk+1 carrying 0, 0, STCYCL and UNPACK 0x6C000000|chunk<<16|vu. Each bone gets two matrices:
   - **Collapsed bone** (+0x94 == bone): B row 3 = node row 3. The two matrices are B × VP and B × A.
   - **Any other bone:** node +0x90 × VP (0x70003AC0), and C × A. C (0x70003480) holds rows 0..2 of the node, each normalised: len² = (x² + y²) + z², len = VU square root, then 1/len by the VU divide, and xyz × 1/len (w = 0). Row 3 is copied raw.
5. **Return value.** The cursor on entry.

Tag byte +2 and tag bytes +8..+0xF are never written. The native module leaves them unchanged too.

**Arithmetic.** Every EE COP1 and VU0-macro instruction goes through `src/game/em_ee_float.h`
(docs/EE_FLOAT_MODEL.md section 6). The module has no float helpers of its own, and the compiled object
contains no host FP arithmetic instruction (checked with `objdump`: 0 fadd/fsub/fmul/fdiv/fsqrt/fcvt/scvtf).
- **EE:** `em_ee_c_lt_bits`, `em_ee_add_bits`, `em_ee_sub_bits` (the 00102A60/B08/BB0 prologue),
  `em_ee_cvt_s_w_bits` and `em_ee_mul_bits` (001C9610 scale), and `em_ee_c_lt_bits` / `em_ee_mul_bits`
  (001CAA00 radius).
- **VU0:** each instruction is one `em_vu_vec_bits(op, dest, bc, …)` call with the form read from the `.s`, so the
  header's measured table chooses the clamps. The forms used:
  - 001029C0: SUB xyzw, ADD w.
  - 001029E8: ADDBC x/0, x/1, x/2, x/3 and xy/0; MUL x; MULBC yzw/0, xyzw/3, xyzw/0, xyz/0, xy/0 and x/0; SUB xyzw; SUBBC w/0 and x/0; ADDQ x; the square root (`em_vu_sqrt_bits`).
  - 00102A60/B08/BB0: SUB xyz and zw; ADDBC x/0, x/1, x/3, y/0, y/1, y/3, z/0, z/1 and w/3; SUBBC x/0, y/0 and z/0; and the row transform.
  - Row transform (rotates, 001C9610, 001C7420): MULABC xyzw/0, MADDABC xyzw/1 and xyzw/2, MADDBC xyzw/3.
  - 00102918: ADD xyz. build_trs_matrix and 001C9610: MULBC xyz/0, xyz/1 and xyz/2.
  - 001C7420: SUB xyzw (the B clear and the normalise's zero row), MUL xyz, ADDBC x/1 and x/2, the square root,
    ADDQ x, the divide (`em_vu_div_bits` form (3,0)), MULQ xyz.
- **Refusals.** A nonzero `em_ee_float` status latches `EM_OWNER_FAULT_UNMEASURED_FORM` (6) at the executing
  routine's address. build_trs_matrix is 0x001C94B0. The SDK entry points return the status. Every form listed
  is in the measured table, so this is unreachable unless the table changes.
- **Register bits across the API.** Original float register values cross as raw `uint32_t`:
  - the SDK angle (f12);
  - 001B1630's f12/f13/f14;
  - 001CA7B0's and 001CA990's radius (f12).
  A signalling NaN therefore keeps its payload. The oracle now passes 0x7F800001 radii and angles unchanged;
  it used to substitute 0x7FC00000. The 001B17A0 cases also put special position bits into f12..f14. A
  mutant that sends those arguments through a host double conversion (which quiets 0x7F800001 into
  0x7FC00001) fails there; the finite-only positions used before did not catch it.
- **Stale VU registers.** The entry values a routine reads without setting them never reach a result:
  - 001C7420's B clear subtracts a VU register from itself (SUB xyzw). The form clamps both operands, and
    both operands are the same register, so every lane is +0 whatever the entry value. The native module
    passes zeros, and still looks the form up before it writes a byte.
  - 001029E8 reads the y, z and w lanes of its entry sine register only to multiply them by 0.0. Lane y is
    then overwritten by the (s, s) store of the polynomial. Lanes z and w are overwritten by the callers'
    zw clear and then by the row reload of the transform loop, before any other read.
  - The entry lanes of the other VU registers these routines use, where they are not written first, are
    never read.

## Verification

`python3 tools/test_owner_services_reference.py` compiles the module and executes the **original instructions**
of every routine above from the pinned ELF (SHA-256 checked). The interpreter has 128-bit GPRs, COP1 and VU0
macro mode, and every float operation goes through `ee_float_model`.

- **Calls.** Every call that leaves the translated set is recorded with its arguments, and both sides answer
  from one script.
- **Bytes.** Every modelled owner, node, scratchpad, display-list and global byte is compared.
- **Writes.** Every byte the original writes outside the stack must lie inside the modelled set.

| Group | Default (quick) | `EM_TEST_FULL=1` |
|---|---|---|
| 001B0FD0 / 001B0EA0 / 001B1020 / 001B0DC0. Bone counts 0..56; the cap at -1/0/±1/0x7FFF; shuffled slots; records with specials; a1/a2/a3 variants. | 120 | 1,600 |
| bone_init_default_1 alone | 12 | 200 |
| 001C6380. Bone counts 0/1/2/3/21/56. Parents: root, earlier, later and self. Scales 0x1000, 0x800 and random. Specials in bind and scale. | 16 | 240 |
| SDK sets: rotate X/Y/Z, 00102C58, 00102918, 00102958. Angles include ±0, ±π, ±MAX, ±Inf, NaN, denormals and huge values. Tiny rows force signed-zero flushes. | 200 | 3,000 |
| 001B17A0: mode × class/flags × kind × +0x0D × +0x2E × result, at a finite +0xB0. Then special +0xB0/+0xB4/+0xB8 bits (± signalling NaN, quiet NaN, -0, ± denormal, ± Inf, rotated through every lane, both modes): the original's f12..f14 must equal the stored bits, and native must equal original. | 400 (covering every value) + 16 specials | 1,296 + 16 |
| 001CAA00 → 001CA990 → 001C7420. Bone counts 0/1/3/21/31/32/56 (both chunk edges). Collapsed bone in and out of range. Pose bone. Model or none. Radius 20.0 edges and specials. Cull -1/0/1/5/0x1F. Attachment. Specials in VP, A, B and the nodes. DL prefilled with a pattern. | 24 | 300 |
| 001B1E20: 16 records × 11 durations (±64-bit, int16 edges) | 176 | 176 |
| 001B5B70: gate × timer edges | 18 | 18 |
| **Capture placements** (below) | 40 owners (every behaviour) | 193 owners |
| **Capture palette** (below) | 4 snapshots / 30 uploads | same |

Timing (M1): the default run takes about 2 s, the full run about 10 s, and `owner_services_test` 0.13 s.

**Capture placements.** The owners come from the user's `playable_ee.bin` and the s87 route snapshots 04, 05,
07 and 08. For each owner:
1. Run bone_init_default_1 and 001C6380 over the captured owner, native and original. They must be equal.
2. Count how many captured `+0xD0` and node `+0x90` matrices the translation reproduces.

The test **asserts** full reproduction for every listed behaviour, and for the truck while it is wedged. Measured
over all 193 owners:

| Behaviour | Owners | +0xD0 reproduced | Nodes reproduced |
|---|---|---|---|
| 001551B0 crates | 20 | 20 | 20/20 |
| 00156620 drums | 10 | 10 | 20/20 |
| 00827630 fan | 10 | 10 | 10/10 |
| 00219550 pickups | 26 | 26 | 78/78 |
| 0015AFA0 pickup 0B | 5 | 5 | 5/5 |
| 00827B10 elevator terminal | 5 | 5 | 5/5 |
| 00159210 panel | 5 | 5 | 5/5 |
| 001C4820 | 5 | 5 | 5/5 |
| 001C5680 indicators | 32 | 32 | 99/99 |
| 00823E80 parachute | 5 | 5 | 5/5 |
| 00825940 husk creature | 5 | 5 | 20/20 |
| 00827490 husk partner | 5 | 5 | 10/10 |
| 00823FF0 truck | 5 | 4 (every wedged state 4) | 4/5 |
| 001C5760 | 5 | 5 | 1/5 |
| 001BC350 door | 5 | 5 | 0/10 (animated nodes) |
| 008237E0 Roger | 5 | 5 | 0/105 (animated skeleton) |
| 0018A6B0 player children | 35 | 0 | 0/35 (placed on player bones) |
| 001C5C90 attachment | 5 | 0 | 0/5 |

Notes on the rows that are not fully reproduced:
- **Truck.** The one miss is snapshot 08, where the truck is at rest (state 2). Its `+0xD0` there is the result of the fall, not a 001C6380 placement.
- **Not asserted.** The rows with partial or zero reproduction are placed by code outside this lane (animation, parenting, attachments). The test reports them and does not assert them.
- **Captured nodes are unanimated.** For every reproduced owner, the captured nodes equal bone_init_default_1's defaults: rot and trans 0, scale 0x1000, and the bind copied from the model.

**Capture palette.** 001C7420 is checked against the captured scratchpad light blocks and the display list. In
all four route snapshots, SPR 0x70003480 holds row 3 of player node 20 (node 0x7D6880), so the player
(0x8102B0, 21 bones) is the frame's last 001C7420 call.

For every captured 21-bone upload in RAM (30 in total), the check does three things:
1. It executes the original 001C7420 over the capture. A = captured 0x70003400, and B is the colour rows of that upload.
2. It asserts native == original, byte for byte, over the whole 2,816-byte upload and the scratchpad.
3. It asserts that at least one upload per snapshot equals the **captured bytes** exactly. Exactly one does in each snapshot: the player's.

For those uploads, native C must equal the captured 0x70003480, and the zeroed B must equal the captured
0x70003440. The node × VP rows and the C × A rows are therefore independent capture evidence for the matrix
product, the VU normalise (square root and divide) and the packet layout. The colour rows alone are circular, because B
is taken from the upload itself.

**Defect injection** (2026-09-23, after the move onto em_ee_float.h). 27 native defects were injected into a
scratch copy of the module, and the default case set caught 26:
- **Form choices:** the coefficient multiply as MULBC xyzw/0 instead of xyzw/3; a MULBC xyz/0 as xyzw; MADDBC
  xyzw/3 as MADDABC; MULABC xyzw/0 as MULBC; 00102918's ADD xyz as xyzw; the normalise's MULQ xyz as xyzw.
- **Lanes and order:** the power-series order; the square-root lane; the identity's lane rotation; the rot-X sign; the rot-Y lane;
  the Euler order; the TRS scale lane; the bind-product order; the normalise sum lane; the identity w lane.
- **EE:** the prologue compare order; the prologue EE add rewritten as an EE subtract of the negated angle;
  the scale int-to-float sign-extension; the radius compare (less-or-equal instead of less-than); the 1.2
  constant.
- **Data and packets:** the sine sign branch; the collapsed B row 3; the C row 3; the B clear; the order of
  the 001B1630 arguments.

Two additions to the default cases came out of this run:
- **Row 3 w specials.** The SDK cases now put -0, a denormal or a signalling NaN into row 3 w, one seed in
  seven. Before this, only the full sweep caught the 00102918 dest-mask defect.
- **Negative NaNs.** `special()` now includes 0xFFC00000 and 0xFF800001. Negative NaNs are the only inputs
  that take the add side of the SDK prologue with a NaN. There the EE add gives +MAX, and an EE subtract of
  the negated value gives -MAX; `ee_float_model` found 569 of 300,000 random patterns that differ this way.

The one survivor turns the VU divide form (3,0) into (0,0). That mutant is **equivalent**: the two forms differ only for a
NaN divisor. Here the divisor is 0 + the VU square root of the squared length, and that square root saturates its input, so the divisor is
never NaN (0 exponent-255 results over 300,000 patterns).

`tests/owner_services_test.c` (ASan/UBSan) pins:
- every fault address and code;
- the latch (a latched service runs nothing);
- the worker order for 001B0FD0, 001B17A0 and 001CAA00;
- the packet headers for 1 and 32 bones (the second chunk is `0x6C0800F8`);
- the untouched tag bytes;
- the buffer bound;
- the (int16) duration and the table default;
- the countdown wrap;
- raw register bits: a signalling-NaN f12 reaches 001B1630 unchanged, and so does a signalling-NaN radius
  at 001CA7B0 (the EE compare reads it as +MAX, so the 1.2 multiply does not run);
- the 1.2 product: 5.0 becomes 0x40C00000;
- the SDK status returns.

## Boundaries (workers; each is required when reached, and a negative result faults)

| Worker | Original | Contract |
|---|---|---|
| `w_001C6120` | 001C6120(bank, id) | Model table lookup. `id` is the full a1 register; the original masks it with 0x7FFF. Returns the handle. |
| `w_001CA6E0` | 001CA6E0 = 001CA5E0(owner, handle, 0) | Binds the model: it must set `owner->model` (+0x44) and whatever else 001CA5E0 writes. |
| `w_001AF780` | 001AF780 | Pops a bone slot from the D_00275BD0 stack (D_00275BCC -= 1). While fewer than 31 slots are free, the original returns 0: `*slot = NULL`. That NULL faults later, at bone_init. |
| `w_anim_bone_array_setup` | 001CB5B0(count) | D_00275B40 = D_00275B48 + 0x110. |
| `w_bone_init_default_2` | 001C63E0(owner, (int16)a3) | Clip bone init (001B1020 with a2 != -1). |
| `w_001B1CE0` | 001B1CE0(owner) | Pushes +0x14 onto D_00275B54 while D_00275B58 < 0x40. No native translation exists yet; `EmActorClassLists.count_b58` only holds the counter. |
| `w_001B1630` | 001B1630(f12, f13, f14) → u8 | Camera cone/range gate. The arguments are the **raw bits** of +0xB0/+0xB4/+0xB8 (`uint32_t`). Native: `em_interaction_visible` (em_interaction_scan). Its position is a `const float[3]`, so the adapter copies the three words into that array with `memcpy` and keeps every bit. |
| `w_001B1B70` | 001B1B70(owner) | Class lists. Native: `em_actor_class_publish_001B1B70` / `em_actor_collision_owner_publish`. |
| `w_001CA7B0` | 001CA7B0(pos, f12 = radius) | `radius` is the raw f12 bits (`uint32_t`). Clip flags 0..0x1F, or -1 (culled). Any other value faults (BAD_RESULT). |
| `w_001D8C20` | 001D8C20(0) | Context +0x246C = mode. |
| `w_001D89D0` | 001D89D0(owner, 0x70003400, 0x70003440, owner+0x80) | Writes A (light) and B (colour). docs/ACTOR_LIGHTING.md covers the mode-0 rig in em_lighting.c, but it is not a bit-exact translation of this entry point: bind only after a byte comparison. The capture palette check above is a ready harness, because captured 0x70003400 is the player's A. |
| `w_001D1F80` | 001D1F80(0, 1, 0) | Display-list call packet. |
| `w_001CA940` | 001CA940(flags, +0x44) | Mesh kernel: 001D3C30 when flags & 1, else 001D38F0. |
| `w_001CB3C0` | 001CB3C0(owner) | The +0x90 attachment draw. |
| `w_001B61C0` / `w_001B6250` | pad actuator start / stop (&D_00810E40) | Rumble. The 001B61C0 gates (D_00810119, block +0x12, D_00275BE0 != 2, force) belong to that worker. |

**Data views** (`EmOwnerServicesWorld`). Each faults with its data address when it is reached and NULL:
- `D_0028A59C`, `D_0028A56C`: bank words;
- `D_0028A490` (+count): anim words;
- `D_00275BCC`: the bone cap;
- `D_00810CA5`;
- `D_00275B40` (+count);
- the scratchpad (0x70003400/3440/3480/3AC0);
- the display-list channels (cursor, end);
- `D_0024D6F0` (+rows): read from the user's ELF;
- `D_00810E56`, `D_00810E68`.

**Deliberate fail-stops.** In each of these cases the original would read or write outside modelled storage,
so the native module faults instead:
- more than 56 bones, because slot 56 would overwrite the +0x1F0 owner scratch;
- a NULL slot;
- a parent index outside the owner's bones;
- a skeleton record past the model's records;
- a NULL model at 001C6150 or bone_init;
- a D_0028A490 or D_0024D6F0 index outside the view;
- a display-list write past `end`;
- a 001CA7B0 result outside -1..0x1F.

**Not modelled:**
- VU registers left behind. No stale vf/ACC/Q value reaches a result. See "Stale VU registers" under
  Arithmetic.
- 001AF780's own stack.
- Whatever the workers write.

## Binding (for the coordinator chain)

`EmOwnerServicesOwner` is a **view**. The canonical homes are split:
- `EmActor` (em_actor_pool.h) holds +0x01..+0x0D, +0x2E, +0x60 (`f60`), +0x94, +0x98, +0xB0 and +0xC0.
- `EmActor` has **no** home for +0x40, +0x44, +0x90's pointer target, +0xD0 or the +0x110 slots. The fan,
  truck, crate and drum modules each keep their own +0xD0.

The coordinator needs:
- a per-record side table for +0x40/+0x44/+0xD0/+0x110;
- a node arena for the 001AF780/001AF800 slot stack.

Load the view from canonical storage before each call and store it back after, as the other hosts do.
Fault codes 1–4 map 1:1 onto `EM_SCENE_FAULT_*`. Code 6, `EM_OWNER_FAULT_UNMEASURED_FORM`, has no
scene counterpart; 5 is already `EM_SCENE_FAULT_FREED_NEXT`. The coordinator must add a scene code for it,
or map it explicitly. It is unreachable unless the em_ee_float form table changes.

| Consumer (original caller) | Worker slot | Call |
|---|---|---|
| Fan 00827630, state 0 | `EmFanOriginalWorkers.w_001B0FD0` | `em_owner_services_001B0FD0`. Return 0/1. **Do not store the view's +0x04 back:** the fan module applies +1 or =3 itself. |
| Fan, tail | `.w_001C6380` | Load `fan->rot_z` into view `rot[2]` (rot x/y and scale from the record), call `_001C6380`, store +0xD0 and the node +0x90. |
| Fan, tail | `.w_001B17A0` | `_001B17A0`. The fan ignores the result; the view's +0x01 is stored. |
| Fan, tail | `.w_draw_4C` | `_001CAA00` (the captured +0x4C of both fans). |
| Truck 00823FF0, state 0 | `EmTruckHooks.model_bind` | `_001B0FD0`: `*pending` = result. The caller overwrites +0x04. |
| Truck | `.placement_matrix` | `_001C6380`, then copy the view's +0xD0 into `matrix`. The view needs the full +0xC0 and +0x60 from the record, not only `rotation_x`. |
| Truck | `.pose` | `em_owner_services_copy_qw4_00102958(D_00275B40[0]->world, matrix)`. |
| Truck | `.rumble` | `_001B1E20(effect, 0)`. |
| Truck | `.draw` | `_001CAA00`. |
| Crates 001551B0, drums 00156620 (both call 001B0FD0 in state 0; their modules split it) | `.allocate_model` | `_001B0EA0`, result 1/0. |
| Crates, drums | `.bone_init` | `_001C62C0`. |
| Crates, drums | `.place` | `_001C6380`, then copy +0xD0 out. |
| Crates, drums | `.draw` | `_001CAA00`. |
| Crates | `.bone_matrix` | `copy_qw4` into `D_00275B40[0]->world`. |
| Drums | `.visibility` | `_001B17A0`, `*visible` = result. |
| Roger 008237E0 | publish (em_roger.c) | `_001B17A0`. |
| Elevator 00827B10 tail (0x827E78), panel 00159210, pickups 00219550, drums 00156620 | ACTOR_COLLISION 7.3 publication | `_001B17A0`, with `w_001B1630` = `em_interaction_visible` (anchor D_008105D0, forward D_00810600, read before the camera update) and `w_001B1B70` = `em_actor_class_publish_001B1B70`. |
| Pickups 00219550 | its 001B1020 call | `_001B1020(owner, a1, a2, a3)` with the caller's registers. |
| Main loop step I (0x1AAF6C, every frame, not gated) | em_frame (ORCH-22/SI-28) | `_001B5B70`, with the D_00810E40 pad block's +0x16/+0x28 views. |

**Not ready to go live.** Several workers have no verified native translation yet:
- 001D89D0 (bit-exact), 001CA7B0, 001CA940 (+ 001D38F0/001D3C30), 001D1F80, 001CB3C0, 001B1CE0;
- the model binders 001C6120/001CA6E0/001CA5E0;
- 001AF780, bone_init_default_2;
- 001B61C0/001B6250.

Until they are bound, keep this module unwired, or bind only the pieces whose workers exist:
- 001C6380 and 00102958 need no workers;
- 001B17A0 needs 001B1630/001B1B70, both translated; 001B1CE0 is reached only in mode 6;
- 001B5B70 needs only 001B6250.
