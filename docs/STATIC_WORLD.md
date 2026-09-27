# Static world and background channel (lane STATIC)

Date: 2026-09-27. Lane b15 / STATIC. New files only:

- `src/game/em_static_world.{h,c}`: the translations this lane adds.
- `src/game/em_static_world_compose.{h,c}`: 001C1D00's whole call tree and
  001D52E0, composed from the verified translations over one set of
  original-address views. This is the entry the binder calls.
- `tools/export_static_world.py`: the static-object bank and the
  background .data block, from the user's own disc and ELF.
- `tools/test_static_world_reference.py`: the original-instruction oracle.

Nothing is bound yet. Every routine below is **verified-unbound**. Section 5
says how to bind them.

Names describe what the instructions do. They are not claims about what the
player sees ("a label is not evidence").

## 1. Status, per function

The Form column says what the translation was read from:
- BM: byte-matched C;
- NM: NEARMISS C, with the splat .s followed;
- asm: asm words;
- und: undecompiled, the .s followed.

| Function | Form | Before (census) | After | Where |
|---|---|---|---|---|
| 001C1D00 | BM | verified-unbound | verified-unbound; the whole tree is now composed (em_swc_001C1D00) | reused: em_frh_001C1D00 |
| 001E0CF0 | BM | verified-unbound | as above | reused: em_rvr_001E0CF0 |
| 001D5370 | NM | verified-unbound | as above | reused: em_render_context_001D5370 |
| 001D52E0 | BM | verified-unbound (UM_001D52E0 reported) | composed: em_swc_001D52E0 | reused: em_render_context_001D52E0 |
| 001E1E60 | NM | "live" as the renderer's own matrix and kernel model (em_background_gs); no packet translation | the channel-3 list, byte for byte | **new**: em_static_world_001E1E60 |
| 001E1AD0 | NM (53%) | not in the census (flag 0x22 is never set on the first level) | unit-verified | **new** |
| 001E0E80 | und | not in the census | unit-verified (inside 001E1AD0) | **new** |
| 001D4FB0 | BM | "live" as the shadow receiver draw (em_shadow_live, a renderer stand-in) | the channel-0 REF run | **new**: em_static_world_001D4FB0 |
| 001D4F30 | NM | not in the census | as 001D4FB0 | **new** |
| 001D4B20 | BM | boundary list (GS/VIF packet build) | the guard-band re-pass | **new** |
| 001D4960 | asm | boundary list | as 001D4B20 | **new** |
| 001D4B10 | BM | boundary list | as 001D4B20 | **new** |
| 001D4A90 | readable C, but wrong | boundary list | as 001D4B20 (read from the .s) | **new** |
| 001D4DA0 | NM | boundary list | the channel-0 upload / REF head | **new** |
| 001D4750 (vif_build_unpack_const) | und | boundary list | as 001D4DA0 | **new** |
| 001D2090 (vif_append_ref_tag) | und | live inside em_owner_draw_original (a static copy) | public memory form | **new** (the owner draw keeps its own copy) |
| 00102958 (copy_qw4) | und | not in the census | inside 001D4750 | **new** |
| 001D6F60, 001D7000, 001D7100, 001D71A0 | und | not in the census | the TEX0, TEXA, upload and MSCAL packets | **new** |
| 00121870 (block_copy) | und | "host memcpy" in other lanes | the exact chunked forward copy | **new** |
| 00102798 | asm | em_camera_commit_00102798 | memory form | reused: em_camera_commit_00102798 |
| 001026D0 | asm | em_sdk_vu0_001026D0 | memory form | reused: em_sdk_vu0_001026D0 |
| 001C6120 | BM | copies inside several modules, each bound to its own bank | memory form over the views | **new** (5 lines) |
| 001D5BD0 | BM | em_area01_render_001D5BD0 (AREA01 lane; not in the Makefile) | a host worker of the composition | reused as is; not reached in AREA11 |

The composition also reuses these verified translations as its workers:
- em_load_veil_particles: 001D1F80, 001D1FF0, 001D2040, 001D7080, 001D6BA0;
- em_render_context: 001D2910, 001D2E00, 001E0CC0;
- em_frh_001D2D20;
- em_rvr_001E2260;
- em_stream_lanes_00128250;
- em_player_float_to_int (001281C0);
- em_sdk_math_original_0011DF78;
- em_effect_original_001026A0.

## 2. What the code does

### 2.1 The static-object bank

`*D_0028A5A0` is 0x01516F40 in every AREA11 capture. It is resource 0x44 of
the area.
- Word 0 is the entry count, 702.
- Words 1..n are entry offsets. 001C6120(bank, id) reads word
  `1 + (id & 0xFFFF & ~0x8000)`, shifts it right by two (signed), shifts it
  back, and adds the bank.
- Entry 0 is the grid that 001D52E0 publishes into context +0x140..+0x167:
  - rows 0x20 and stride 0x20;
  - six floats;
  - from +0x20, 32 x 32 cells of 4 object ids.
- Entries 1..701 are objects. Each has:
  - its block count at +0;
  - the AABB at +0x10 and +0x20 (001D5370 tests the eight corners);
  - its blocks from +0x40, 0x820 bytes each. Together the objects hold
    1,389 blocks.

### 2.2 The channel-0 static run (001D5370's callees)

001D5370's walk and clip test are unchanged (RENDER_CONTEXT.md 2.5). What
it emits comes from these callees:

- **001D4DA0**, once at the start and again after every guard-band object:
  1. 001D4750(0);
  2. 001D2090(0, 0x00237180): the level kernel CALL;
  3. 001D1F80(0, 1, 0);
  4. a REF of 8 quadwords to the skin record D_00816440 + (context +0x9C << 7).
     The context word is loaded once.
- **001D4750(chan)**:
  - It first rewrites the 32-word constant block D_00817240..BF:
    - four rows (1, 0, 0, 0), (0, -1, 0, 0), (0, 1, 0, 0), (0, 0, 0, 1);
    - (32, 32, 32, 0), zeros, (32, 32, 32, 0);
    - three words 0x4B000040 and one 0x4B000080.
  - Then it writes two CNT packets at the channel cursor:
    - 9 quadwords: the VIF codes FLUSH and UNPACK V4-32 of 8 quadwords to
      VU address 0, then the four quadwords at 0x70003AC0 and the first
      four of the block;
    - 5 quadwords: UNPACK of 4 quadwords to VU address 0x3F5, then the
      last four quadwords of the block.
  - Each copy (copy_qw4) loads all four quadwords before it stores any.
- **001D2090(chan, target)**:
  1. a REF of one quadword to `*D_00275674`;
  2. context +0x50 + 4 chan = target;
  3. a CALL tag to target.
- **001D4FB0(obj) = 001D4F30(0, obj)**, and **001D4B10(obj) =
  001D4A90(0, obj)**, which does the same thing:
  - The block count at obj +0 is taken in runs of at most 0x1F8.
  - Each run gets one REF tag:
    - address = obj + 0x40 + 0x820 x (the blocks before this run);
    - quadword count = 0x82 x (the run), a halfword store.
  - A count of 0 or less emits nothing.
  - 001D4A90's readable C is wrong (it keeps one run length for every run,
    and it stores through a bogus pointer). The instructions were followed.
- **001D4B20(obj)**, for an object that is partly outside the guard band:
  1. 001D4960: 001D4750(0), 001D2090(0, 0x00239C90) (the clip kernel), and
     the skin-record REF;
  2. then 001D4B10(obj).

Every tag field is stored after its own reload of the cursor word, as the
original does. So are the byte +3 id, the word +4 address, the halfword
+0 count and the cursor advance.

### 2.3 The background channel list (001E0CF0 -> 001E1E60)

001E0CF0 (em_rvr) zeroes +0x1D8 / +0x1E8 through 001E0CC0. Under flags 0x20
and 0x21 it stores 001E1E60's result at +0x1D8. Under 0x20 and 0x22 it
stores 001E1AD0's result at +0x1E8.

001E1E60(a0, chan) does not read a0 (001E0CF0 passes ctx + 0x180). The
routine:
1. The list start is the channel cursor. It is also the return value.
2. 001D1F80(chan, 0, 7), then 001D1FF0(chan, 0).
3. 001D6F60(chan, the 64-bit TEX0 at ctx +0x1D0, 0x80). This is a CNT of 5
   quadwords: DIRECT, then a GIF A+D tag of three registers: TEXFLUSH 0,
   TEX0_1, TEXA = 0x80.
4. RGBAQ = int(128 a) << 24 | int(128 b) << 16 | int(128 g) << 8 |
   int(128 r), from the floats at ctx +0x1C0..+0x1CC through 00128250. The
   context word is loaded once; each float is loaded after the previous
   call. Then 001D7080(chan, rgbaq, 1.0).
5. The matrix D_00253570:
   - D_00253570 = the transpose of ctx +0x2380 (00102798);
   - the second row, D_00253580..8C, is doubled word by word (value x 2.0);
   - 001026D0(D_00253570, the X/Y swap, D_00253570). The swap rows are
     (0,1,0,0), (1,0,0,0), (0,0,1,0), (0,0,0,1) and it is the routine's a
     operand.
6. D_002535B8 = the zoom at ctx +0x2468.
7. Four 001D7100 uploads: D_00253560 (16 bytes) to VU 0, 0x81 and 0x102,
   and D_00253570 (0x80 bytes) to VU 0x200. Each one is:
   - a CNT of (n >> 4) + 1 quadwords;
   - STCYCL 4,4;
   - UNPACK V4-32 of n >> 4 quadwords;
   - the data by block_copy.
8. 001D2090(chan, 0x0023C990), the grid kernel. 001D71A0(chan, 0) is a CNT
   with MSCAL 0.
9. Under flag 0x23 (never set on the first level) the sound branch runs:
   - 001D2910(0x24) picks the id 0x4B4E4514 with level 16 + 16 r, or the id
     0xCDC3D411 with 32 + 32 r, where r = rand x 2^-31;
   - 001D73A0 stops the old handle D_0027569C unless it is -1, and
     001D72D0 starts a new one, stored back to D_0027569C;
   - 001D1F80(chan, 2, 2);
   - 001D75E0(chan, id block, 0x2007780621322A00, 0x80808080, 384, 384);
   - 001CEFD0(id block, levels) runs only when the stop returned 0.
10. 001D2040(chan, 1), then a RET tag (0x60) at the cursor.

The list is 0x250 bytes on the first level.

### 2.4 001E1AD0 (flag 0x22; not reached on the first level)

1. The list start is the channel cursor.
2. It reads the phase:
   - r = 001D2E00(1). A negative r is halved (logical) with its low bit
     or-ed back, converted, and doubled.
   - D_00275C0C -= r / 65535. While D_00275C0C < 0, it adds 1.0. The word
     is reloaded around every step.
3. 001E1760(chan), 001D6BA0(chan, 0x258000, 8, 8, 0, 1), 001D1F80(chan, 0,
   1), 001D1FF0(chan, 1), then 001D7000(chan, 0x80): a CNT of 3 quadwords
   carrying TEXA = 0x80.
4. 32 rows. Each row is:
   - a CNT of 0xC2 quadwords (the cursor advances 0xC30);
   - DIRECT 0xC1;
   - the GIF tag 0x602E400000008020 / 0x512512;
   - 32 cells of 0x60 bytes.
5. For every cell:
   - u = float_to_int(-256 + 512 col / 31);
   - vt and vb = float_to_int(-112 + 224 row / 31) and the same for row + 1;
   - 001E0E80(cell, u, 2 vt) is stored at +0x1C, and 001E0E80(cell + 0x30,
     u, 2 vb) at +0x4C;
   - the phase is added to +0x04 and +0x34 (reloaded for each);
   - the RGB words at +0x10..+0x18 and +0x40..+0x48 are 0x80;
   - the XY words are ((u + 0x800) << 4, (vt + 0x800) << 4) and the same
     with vb;
   - +0x2C and +0x5C are 0.
6. 001D1F80(chan, 3, 8), 001E17E0(chan), then the RET tag.
7. 001E0E80(out, x, y):
   - The ray is 001026A0(transpose(ctx +0x2380), (x, y, zoom, 0)).
   - If ray.y is not below 0, it writes out = (0, 0, 1.0) and returns 0.
   - Otherwise, with d = D_008105D4 - 10 and k = d / ray.y, it writes out =
     (0.002 ray.x k, 0.002 ray.z k, 1.0). It returns float_to_int(256
     |ray.y| / max(1.0, d)), clamped to 0x80. A negative result would give
     0; that is unreachable, because |ray.y| is not negative.

### 2.5 Faults

All faults are fail-stop:
- A NULL worker faults before the entry writes anything.
- A negative worker result faults at once.
- An address no view covers faults with EM_SW_FAULT_BAD_ADDRESS.
- A misaligned word, halfword or doubleword access also faults with
  EM_SW_FAULT_BAD_ADDRESS. The EE takes an address error there. The byte
  store before it has landed, exactly as on the hardware.
- A store into a view marked read-only (`EmStaticWorld.read_only`, set
  from `EmSwc.read_only`) faults with EM_SW_FAULT_READ_ONLY before the
  store.
- The first fault latches. Every later call returns -1. The entry that
  faulted still restores the caller's function on its way out (LEAVE and
  the early-return macros ETRY / ENEEDW), so a caller that clears the fault
  and reuses the state starts from a clean attribution.

The composition keeps the innermost fault, with its module, function, code
and detail. What no translation covers is the host's (EmSwcHostWorkers).
A NULL host worker faults when it is reached.

## 3. The export

`python3 tools/export_static_world.py` writes the ignored
`assets/scene_snow/static_world.emsw`. It runs on native arm64 macOS and is
pure Python.

- **The bank.** The source is the chunk15 files concatenated in index
  order. The bank starts at offset 0x304000 (f12_id44 + 0x123000) and runs
  into f13..f17. The extent is read from the bank's own table: the grid's
  rows x stride x 4 ids, and each object's blocks. That gives 0x2D6FE0
  bytes: 701 objects and 1,389 blocks.
- **The .data block.** D_00253560..D_002535EF (0x90 bytes) comes from the
  pinned ELF. It holds the GIF tag, the matrix slot and the kernel
  constants that 001E1E60 uploads.
- **Verification** (default: every capture). For each capture it checks:
  - the key is 0x0B00;
  - D_0028A5A0 = 0x01516F40;
  - the whole bank equals RAM byte for byte;
  - the .data block equals RAM, except the words 001E1E60 rewrites every
    frame (D_00253570..AF and D_002535B8).

  Result: 19 AREA11 captures equal (the opening, playable and handoff images,
  route beats 00..14 and the c7cap one). 15_level_exit (key 0x0100) is
  listed and skipped. The whole rest of the concatenation from 0x304000
  (0x48D000 bytes) also equals RAM, as RENDER_CONTEXT.md 8.4 found. The
  export keeps only the bank's own extent.
- **Format.** 'EMSW', version 1, the block count and the bank address. Then
  (address, size, offset) rows and the block bytes, 16-aligned. Block 0 is
  the bank; block 1 is the .data block.

## 4. Verification

`python3 tools/test_static_world_reference.py`:
- default: about 5 s wall, about 10 s CPU over 4 workers (the shim
  dylib is rebuilt only when a source changes; the first run after a change
  adds about 2 s);
- `EM_TEST_FULL=1`: about 35 s (312 cases, 144,400 checkpoints).

The oracle is the render-context lane's RenderEE (FallEE with VCLIPW and
the CLIP register) plus the MMI PEXTLW / PEXTUW that 00102798 needs. It
executes the ORIGINAL routines, with all their callees, over captured RAM
and scratchpad. Only the host workers are stubbed, identically on both
sides.

At the entry of every traced routine, at any depth (39 addresses), the
oracle records three things: the address, the argument registers, and
every RAM / scratchpad byte written since the previous entry. The native
side reports the same entries through the em_static_world trace hook. At
each one the shim compares:
- the address;
- the arguments, stack addresses excepted;
- those bytes in its own image.

So memory is checked at every callee entry. After the call, the whole 32 MB
RAM, the scratchpad, the entry count and the return value must be equal.

The host workers (the stubbed callees) check the values they are actually
called with, not the trace written just before the call: their own address
is the checkpoint just passed, and every scalar argument (0x001D73A0's
handle, 001D75E0's channel, 64-bit a2, a3, f12 and f13, 001E1760's and
001E17E0's channel), besides the id and level blocks, must equal the
original's registers there. A call that passed different values than the
trace reported fails (checked by hand: 001D75E0's a3 and 001E17E0's
channel changed in the call alone both fail).

- **A. The whole tree.** 001C1D00 over the captures as captured. The
  default run uses the opening; the full run uses 17 captures (the opening,
  beats 00..14 and the c7cap one).
- **B. The captured lists.** These are rebuilt from the capture's own
  state. The default run uses the opening and 14; the full run all 17.
  - **Channel 3.** The cursor is set to the captured +0x1D8 and 001E0CF0
    runs. It rebuilds exactly the captured 0x250-byte list, D_00253570..AF
    and D_002535B8, and puts the start back at +0x1D8.
  - **Channel 0.** The cursor is set to the captured static-world start
    (channel-0 base + 0x10 in every capture), and D_00810610 to the view the
    frame used (ctx +0x2380, which the frame head copied before the camera
    moved). 001D5370 then rebuilds exactly the captured run, 0xB00..0x3150
    bytes per capture.
  - The scratchpad clip matrices at 0x70003400 are not compared. Later
    routines of the same frame reuse that scratchpad.
- **C. Units.** These cover:
  - 001E1E60 with flags 0x23 / 0x24, the handle, random colours, views,
    zooms and channels;
  - 001E1AD0 (positive and negative rand words) and 001E0E80, including an
    identity view that hits the 128 / 129 / 256 alpha boundaries and the
    d < 1 path;
  - run boundaries for 001D4F30 / 001D4A90 (0, -1, 1, 0x1F7..0x1F9, 0x3F0,
    0x3F1, 0x5E9);
  - the object entries over real bank objects;
  - the leaf builders on every channel;
  - 001D7100 sizes and alignments;
  - block_copy overlaps and alignments, including addresses that are 8 but
    not 16 aligned with n >= 0x20 (both, dst only, src only; these take the
    byte loop);
  - 001026D0 aliasing (dst = a, dst = b);
  - 001C6120 ids with bit 15 and above 0xFFFF;
  - 001D5370's other loop-bound keys (default: 0x1300 room 5, 0x0D00 room
    3, 0x0100; full adds 0x1300 rooms 4 / 2 and 0x0D00 room 4);
  - 001C1D00 with key 0x1500 and flag 0x24 (full adds state bytes 0 / 2
    and key 0x1500 without flag 0x24);
  - 001E0CF0's flag scripts (default 010, 100, 111; full adds 101).

  Both outcomes of all 25 conditional branch sites are reached, except one
  unreachable outcome, listed with its reason. Every call target of the
  translated routines is either traced or stubbed.
- **D. Fail-stop.** Each case faults at the expected point:
  - flag 0x23 without host workers faults at 00122BB8;
  - key 0x0100 without host workers faults at 001D5BD0;
  - with no bank view, 001D52E0 faults;
  - a misaligned cursor faults at 001D4750's first word store, with only
    the tag byte written;
  - a latched fault stops the next entry.
- **E. The binding rehearsal.** 001C1D00 runs through em_swc over 26
  separate views: exactly the 20 that em_render_context_live's
  `build_views()` makes once the binder and the frame loop have handed over
  every external view (section 5.2), with its read-only marking, plus the
  six ranges of section 5.2 (the bank and D_0028A5A0 read-only). There is no
  fault, every view equals the original's bytes, and the original writes
  nothing outside them and nothing inside a read-only one. The marking is
  enforced: with D_00253560 marked read-only, 001E1E60's first store there
  faults (em_static_world, EM_SW_FAULT_READ_ONLY). The default run uses the
  opening; the full run all 17.
- **F. Sanitizers** (full mode only). ASan/UBSan run 001C1D00 over the
  opening under all eight states of flags 0x22..0x24: clean.

Mutations checked by hand (the lane's), each failing the default run:
- the run limit 0x1F7;
- the second-row doubling skipping one word;
- the skin record shift << 6;
- a cell +0x24 from the wrong row;
- the RET tag moved before 001D2040 (caught at the checkpoint, not only at
  the end);
- 001D4750's UNPACK code off by one.

### 4.1 The review's mutation survivors

The review's sweep (47 single-operation mutants; 43 killed) left four. One
was a test gap and is now killed by a pinned case; three are equivalent:

- **m21, block_copy's quadword test `(src | dst) & 15` -> `& 7`: killed.**
  The new C 00121870 cases #8..#11 (dst / src at 8 mod 16, n = 0x40 and
  0x20) fail it at #8 (checked: RAM differs at dst). The original takes the
  byte loop there; the mutant takes the quadword path, whose quadword loads
  and stores ignore the low four address bits, and copies the wrong bytes.
- **m20, block_copy's doubleword loop `n >= 8` -> `n > 8`: equivalent.**
  The doubleword loop is only entered after the quadword loop, so n is
  0..0x1F there and src and dst start 16-aligned and stay 8-aligned. The
  two differ only when a pass sees n == 8: the original copies those 8
  bytes with one doubleword load and store, the mutant leaves them to the
  byte loop, which copies them one by one, forward from the same
  addresses. The same bytes land at the same addresses and both end with
  n == 0. Neither can take an address error (8-aligned doubleword, byte
  accesses). The only observable difference is a host artefact: an 8-byte
  access must lie in one view, byte copies may cross two adjacent views.
  The original has one flat RAM, so this is not original behaviour.
- **m37, 001E0E80's clamp `n < 0x81` -> `n < 0x80`: equivalent.** The
  result is `n < 0 ? 0 : (n < 0x81 ? n : 0x80)`. The two compares differ
  only at n == 0x80: the original returns n = 0x80, the mutant the clamp
  value 0x80. The same for every input.
- **m39, 001E1AD0's `(rnd >> 1) | (rnd & 1)` -> `rnd >> 1`: equivalent on
  the EE.** That operand is only formed when rnd is negative as a signed
  word, so rnd >> 1 lies in [2^30, 2^31 - 1]. CVT.S.W truncates on the EE
  (em_ee_float.h, `em_ee_cvt_s_w_bits`): a value in [2^30, 2^31) has a unit
  in the last place of 2^7, so the low seven bits, the OR'd bit 0 among
  them, are dropped whatever they are. The sticky bit matters only for a
  round-to-nearest conversion, which the EE does not do. Kept as the
  original computes it.

Native cost (M1, -O2): 001C1D00 takes about 1.7 ms per call. Of that,
001D5370's grid walk and VU0 corner tests take 1.70 ms; they are
em_render_context's. 001E0CF0 takes 6 µs.

## 5. Binding (for the chain)

The binder calls **em_swc_001C1D00(&swc, 0x8101D0)** where the frame calls
001C1D00. The views are exactly em_render_context_live's (its own storage
and every external view, with its read-only marking) plus the six
additions below. Rehearsal E proves that exact set of 26 views.

### 5.1 Callers

- **001C1D00.** 0x1AE040's frame variants 001AE5E0 and 001AE6B0 call it
  right after 001D1C50 (em_scene_frame.c `SF_CALL(..., 0x1C1D00u, w_001C1D00,
  EM_SCENE_D_008101D0)` in both). em_scene_bindings.c `w_001C1D00` routes it
  to the stand-in em_render_001C1D00 (em_render_frame.c, `render_env_init`,
  empty).
  - Replace that with a new `em_rcl_001C1D00()` in em_render_context_live.c.
  - `em_rcl_001C1D00` builds an EmSwc over its views, latches
    `c.fault.address` into em_rcl's fault, and returns -1 on a fault.
  - Keep em_render_001C1D00 only for scenes without the render context.
- **001D52E0.** At the area load, 001C1DC0 -> 001C1E70 -> 001D52E0
  (em_rvr_001C1E70). em_render_context_live's `EmRclWorkers.w_001D52E0` is
  the binder's report today (em_scene_bindings.c `rcl_grid_header` ->
  `unmirrored(UM_001D52E0)`). Bind it to `em_rcl_001D52E0()` ->
  `em_swc_001D52E0`. The bank must be in the views by then.
- **Direct binding, if the binder prefers it.** It can instead bind
  em_frh's `w_001E0CF0` / `w_001D5370` (em_render_context_live.c `wire`,
  today `unbound`) to `em_swc_001E0CF0` / `em_swc_001D5370`. The result is
  the same.

### 5.2 Views

**Build a separate view array for em_swc.** Do not cast em_rcl's
`rc_views` to EmStaticWorldView: the layouts agree today
(`{address, size, bytes}`), but nothing ties them. Fill one
`EmStaticWorldView swc_views[32]` and one `uint8_t swc_read_only[32]` from
the same `add_view()` calls, and pass `swc.read_only = swc_read_only`.

**Read-only marking.** em_rcl marks views read-only only for em_frh
(`EmFrhView.writable`; its `rc_views` and `pc_regions` have no such flag).
em_swc keeps that marking: `EmSwc.read_only[i] != 0` gives
`EmFrhView.writable = 0` for em_frh and makes em_static_world's stores
into that view fault. em_render_context and em_load_veil_particles have no
read-only notion, in em_rcl or here. Rehearsal E checks that the original
writes nothing inside a view marked read-only, and that the marking is
enforced.

**Capacity.** em_rcl's `frh_views`, `rc_views` and `pc_regions` hold 24
entries. `build_views()` adds 10 of its own and up to 10 external ones
(X_COUNT); with the six additions below that is 26. The binder must enlarge
those arrays (to 32, say) and the em_swc arrays alike; em_swc accepts up to
32 views (MAX_VIEWS in em_static_world_compose.c) and faults on more.

The 20 views em_render_context_live builds (em_render_context_live.c,
`build_views()`; read-only marked RO):
- the arena ARENA_BASE..ARENA_END, 0x0028F700..0x0076B5BF (the chain
  table D_007635C0 ends at 0x0076B5C0);
- the context through the skin records CTX_BASE..CTX_END,
  0x00811CC0..0x0081723F;
- D_00250F30..D_0025317F (0x2250 bytes);
- D_00275670..9F;
- the scratchpad 0x70003A40..0x70003B3F, 0x70003B60..63 and
  0x70003B70..73;
- RO: D_00241010 (8 bytes), D_0026E510 and D_0026E850 (16 bytes each);
- RO, the external views: D_00810E80 (2), D_00810610 (0x40, the camera
  pool's view), D_008105E0 (0x10), D_008106B0 (0x48), D_00810700..702,
  D_008101E4 (1), 0x70003B8D (1), D_008102B0 (0x320), D_00810E88 (2),
  D_008106C4 (1).

To add:

| Range | Size | Owner / source (RO = mark read-only; rehearsal E proves the tree never writes them) |
|---|---|---|
| 0x01516F40 | 0x2D6FE0 | RO. The bank: block 0 of static_world.emsw, loaded at the area bind (fail-stop if missing, like render_context.emrc) |
| 0x0028A5A0 | 4 | RO. The bank address word (the loader's D_0028A490[0x44]). Before adding an owner, check whether em_module_loader's `d28A5A0` is meant to be this word; if not, store the export's bank address at the area bind |
| 0x00253560 | 0x90 | block 1 of static_world.emsw (001E1E60 rewrites 0x253570..AF and 0x2535B8 every frame) |
| 0x00817240 | 0x80 | .bss; 001D4750 rewrites all of it on every call (start zero) |
| 0x70003400 | 0x80 | the scratchpad clip matrices; 001D5370 writes them before it reads them (start zero) |
| 0x008101D0 | 1 (hold 0x10) | 001C1D00's state byte. 001AF690 zeroes 0x10 bytes there at 0x1AE040 state 0 (the port's w_001AFCA0; em_slg_001AF690 is translated but not live); 001C1D00 sets it 0 -> 1 on its first run. The body is the same for states 0 and 1 |

Not needed on the first level: D_00275C0C and D_008105D4 (001E1AD0 only). A
missing view faults if it is reached.

### 5.3 Host workers

Bind all eight EmSwcHostWorkers to a fault:
- 001D5BD0: key 0x0B00 is not in its list, and AREA11's D_0028A5A4 is 0.
  Bind em_area01_render_001D5BD0 only when another area needs it.
- the sound branch: 00122BB8, 001D73A0, 001D72D0, 001D75E0, 001CEFD0.
  Flag 0x23 is clear in every capture (+0x174 = 3).
- 001E1760 and 001E17E0: flag 0x22 is cleared by 001C1F50 for key 0x0B00.

### 5.4 Makefile

- Add `src/game/em_static_world.c` and `src/game/em_static_world_compose.c`
  to COMMON. Every module they link is already in COMMON:
  - em_render_context, em_render_verify_rest, em_frame_render_heads;
  - em_load_veil_particles, em_stream_lanes_original,
    em_player_stage_workers;
  - em_sdk_math_original, em_effect_original, em_sdk_soft_float;
  - em_camera_commit_original.
- Add a target `test-static-world-reference: python3
  tools/test_static_world_reference.py`.
- Add STARTUP.md an exporter row for tools/export_static_world.py.

### 5.5 What to retire or update in the binding commit

- **em_render_001C1D00 / render_env_init** (em_render_frame.c) on the
  render-context path. Its census row changes accordingly.
- **The flag-0x21 stand-in in `background_gate`** (em_render_frame.c).
  - 001E0DF0 (decomp func_001E0DF0.c, byte-matched) releases the list
    only when 001D2910(0x20) != 0 AND the +0x1D8 word != 0 (then +0x1E8 the
    same way). The replacement gate must test both: the render flag 0x20
    and the word, which is now built.
  - The native background draw can then be checked against the list's
    TEX0 / RGBAQ packets at +0x1D8 instead of the context words.
  - em_background_gs_matrix duplicates the D_00253570 computation. It may
    read the matrix and D_002535B8 from the storage instead, or be kept
    and compared.
- **Step V's missing CALL** (C7 limitation; RENDER_CONTEXT.md 9.4). A world
  frame's main list now holds the +0x1D8 CALL: seven tags, as in the
  captures.
  - The level smoke's `check_render_context` must expect it.
  - 001D21B0 becomes live, reached by 001E0DF0.
- **Reports.** UM_001D52E0 and `rcl_grid_header` go.
- **Docs.** Update:
  - RENDER_CONTEXT.md 8.4 (the 001C1D00 and 001D52E0 rows);
  - FIRST_LEVEL_AUDIT.md item 3 (the bank export and the background channel
    are done; the renderer decision remains);
  - FIRST_LEVEL_CENSUS.md rows 001C1D00, 001D5370, 001D52E0, 001E0CF0,
    001E1E60, 001D4FB0 and the boundary-list entries 001D4750, 001D4960,
    001D4A90, 001D4B10, 001D4B20, 001D4DA0;
  - FIDELITY_FEATURES.md ("the +0x1D8 channel-3 list is a native stand-in";
    "001C1D00's empty render-env step" among the stand-ins).
- **Frame cost.** Measure the added 1.7 ms per world frame on the live
  build (section 4).

### 5.6 The renderer boundary (not decided here)

Once bound, the channel-0 run holds, per frame:
- the objects the original draws, in its order, as REF tags into the bank
  (object + 0x40 + 0x820 k, 0x82 quadwords per block);
- the kernel for each: CALL 0x00237180 for the level kernel; CALL
  0x00239C90 after 001D4B20 for the guard-band clip kernel;
- the skin-record REFs.

The port still draws the level from exported zone meshes (LEVEL_MATERIALS.md
"PARTIAL"). Nothing consumes the run yet. It is not part of the chain page
(em_chain_page_live walks the page at context +0x00 / +0x04, not channel 0),
so binding does not change any draw. Drawing the static world from the run
would reproduce the original's culling and order; that is the renderer
decision FIRST_LEVEL_AUDIT item 3 still lists.

## 6. Known gaps

- **001E1AD0's 001E1760 and 001E17E0 are not translated.** Nor are
  001E1760's 001D6E60 path, 001E10A0 and 001E13E0. They are host workers
  (flag 0x22; the first level never sets it). The unit oracle stubs them on
  both sides.
- **The flag-0x23 sound branch's callees are not translated** (00122BB8,
  001D73A0, 001D72D0, 001D75E0, 001CEFD0). They are host workers; their
  arguments, including the id and level blocks, are compared.
- **001D5BD0 is not bound in the composition.** The AREA01 lane's verified
  em_area01_render_001D5BD0 is not in the Makefile.
- **The capture comparison covers only the captured frames' lists** (17
  frames). Per-frame lists over the full route need the level smoke (after
  binding).
- **The scratchpad matrices 0x70003400..7F are not compared with the
  captures** (they are reused later in the frame). They are compared with
  the original, run over the same input.
- **The 001D5370 clip rule (VCLIPW) is em_render_context's documented
  model** (RENDER_CONTEXT.md section 4). It is the same on both sides of the
  test.
- **The trace hook exists only for the test.** It is NULL in the game.
- **Decomp-side registry (not this lane's files).** The decomp's
  docs/FUNCTIONS.csv lists 001D4A90 as `readable`, but its C
  (src/func_001D4A90.c) is wrong: it keeps one run length for every run,
  stores through a bogus pointer and dereferences *D_00275670 (the review
  confirmed it against the .s; this lane translated from the .s). It also
  lists 001E0DF0 as `undecompiled`, although src/func_001E0DF0.c is a
  byte-matched C body. Both rows need a decomp-side correction.
