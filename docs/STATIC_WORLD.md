# Static world and background channel

Translated 2026-09-27 (lane b15 / STATIC); bound live and drawn from its
packets 2026-09-28 (chain step "Static world and background channel live").
Files:

- `src/game/em_static_world.{h,c}`: the static-object packet builders and
  the background channel 001E1E60 / 001E1AD0.
- `src/game/em_static_world_compose.{h,c}`: 001C1D00's whole call tree and
  001D52E0, composed from the verified translations over one set of
  original-address views. The render context calls it
  (`em_rcl_001C1D00`, `em_rcl`'s 001D52E0 worker).
- `src/game/em_vu1_level_kernel.h`: the level kernel 0x00237180 (79
  instructions), translated from its VU1 microcode (section 7).
- `src/game/em_static_world_draw.{h,c}`: the channel-0 run as the DMA sends
  it to VU1 and the GS (section 7).
- `src/game/em_static_world_live.{h,c}`: the run drawn at the frame close,
  and the channel-3 list's GS writes for the background gate.
- `tools/export_static_world.py`: the static-object bank and the
  background .data block, from the user's own disc and ELF.
- `tools/test_static_world_reference.py`: the original-instruction oracle of
  the builders; `tools/test_static_world_draw_reference.py`: the run's
  draw against the original VU1 microcode; `tools/test_static_world_gpu.py`
  (+ `tests/static_world_gpu_test.c`): the Metal pixel path;
  `tools/level_smoke_static_world.py`: the level smoke's check.

Every routine below is **live** on the first level (section 5). The level
is drawn from the original packets; the legacy zone meshes no longer load
in AREA11 (section 7.4).

Names describe what the instructions do. They are not claims about what the
player sees ("a label is not evidence").

## 1. Status, per function

The Form column says what the translation was read from:
- BM: byte-matched C;
- NM: NEARMISS C, with the splat .s followed;
- asm: asm words;
- und: undecompiled, the .s followed.

| Function | Form | Status | What | Where |
|---|---|---|---|---|
| 001C1D00 | BM | live (both world variants) | the whole tree (em_swc_001C1D00) | em_frh_001C1D00 |
| 001E0CF0 | BM | live | the background channel | em_rvr_001E0CF0 |
| 001D5370 | NM | live | the grid pass | em_render_context_001D5370 |
| 001D52E0 | BM | live (the area load's 001C1E70) | the grid header | em_render_context_001D52E0 |
| 001E1E60 | NM | live | the channel-3 list, byte for byte | em_static_world_001E1E60 |
| 001E1AD0 | NM (53%) | unit-verified; not reached (flag 0x22 is never set on the first level) | | em_static_world |
| 001E0E80 | und | unit-verified (inside 001E1AD0) | | em_static_world |
| 001D4FB0 | BM | live | the channel-0 REF run | em_static_world_001D4FB0 |
| 001D4F30 | NM | live | as 001D4FB0 | em_static_world |
| 001D4B20 | BM | live | the guard-band re-pass | em_static_world |
| 001D4960 | asm | live | as 001D4B20 | em_static_world |
| 001D4B10 | BM | live | as 001D4B20 | em_static_world |
| 001D4A90 | readable C, but wrong | live | as 001D4B20 (read from the .s) | em_static_world |
| 001D4DA0 | NM | live | the channel-0 upload / REF head | em_static_world |
| 001D4750 (vif_build_unpack_const) | und | live | as 001D4DA0 | em_static_world |
| 001D2090 (vif_append_ref_tag) | und | live | the one translation is em_owner_draw_vif_append_ref_tag; em_static_world's memory form checks the views, then runs it | em_owner_draw_original |
| 00102958 (copy_qw4) | und | live (inside 001D4750) | four quadwords, all loaded before any store | em_static_world (memory form; the owner services' host form is a memmove) |
| 001D6F60, 001D7000, 001D7100, 001D71A0 | und | live (001D7000 only inside 001E1AD0) | the TEX0, TEXA, upload and MSCAL packets | em_static_world |
| 00121870 (block_copy) | und | live (inside 001D7100) | the exact chunked forward copy | em_static_world |
| 00102798 | asm | live | memory form | em_camera_commit_00102798 |
| 001026D0 | asm | live | memory form | em_sdk_vu0_001026D0 |
| 001C6120 | BM | live | memory form over the views (5 lines; several modules keep their own copy over their own bank, as before) | em_static_world |
| 001D5BD0 | BM | not reached in AREA11 (key 0x0B00 is not in its list) | a host worker of the composition, bound to a fault | |
| VU1 0x00237180 | microcode | live | the level kernel (section 7) | em_vu1_level_kernel.h |
| VU1 0x00239C90 | microcode | live | the guard-band clip kernel | em_vu1_shadow_clip.h (the box kernel of the shadow chain, the same program) |

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
- **E. The binding rehearsal.** 001C1D00 runs through em_swc over 27
  separate views: exactly the ones em_render_context_live's
  `build_views()` makes once the binder and the frame loop have handed over
  every external view (section 5.2), with its read-only marking (the bank,
  D_0028A5A0 and the other external views read-only). There is no
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

## 5. The binding (live)

### 5.1 Callers

- **001C1D00.** 0x1AE040's frame variants 001AE5E0 and 001AE6B0 call it
  right after 001D1C50 (em_scene_frame.c `SF_CALL(..., 0x1C1D00u,
  w_001C1D00, EM_SCENE_D_008101D0)`). em_scene_bindings.c `w_001C1D00`
  runs `em_rcl_001C1D00(0x8101D0)` when the render context is live (the
  first level). `em_rcl_001C1D00` builds an EmSwc over its views, calls
  `em_swc_001C1D00`, latches the innermost fault's address as the render
  context's fault (fail-stop) and records the channel-0 run it wrote
  ([cursor before, cursor after): `em_rcl_static_run`, cleared by the frame
  head 001D1AE0). A scene without the render context keeps
  em_render_001C1D00 (em_render_frame.c, the empty render-env step).
- **001D52E0.** The area load's 001C1DC0 -> 001C1E70 -> 001D52E0
  (em_rvr_001C1E70) reaches em_rcl's own worker, `em_swc_001D52E0` over the
  same views. The binder's report (`rcl_grid_header`, UM_001D52E0) and the
  EmRclWorkers slot `w_001D52E0` are gone.
- **001E0DF0 / 001D21B0.** Step V (001D2300, em_frame_kick) already ran
  001E0DF0 every world frame; with +0x1D8 built it now tags the channel-3
  list through 001D21B0: a world frame's main list holds seven tags, as in
  every route capture.

### 5.2 Views

em_render_context_live keeps one view list for all its lane modules
(`add_view`: EmFrhView with the writable flag, EmRenderContextView,
EmPacketChainRegion) and, from the same calls, the EmStaticWorldView array
and its read-only flags (a view is read-only for em_static_world exactly
when it is not writable for em_frh). Capacity: 32 (`VIEWS_MAX`, faulting
beyond it); a first-level bind has 27:

- its own storage: the arena 0x0028F700..0x0076B5BF, the context through
  the skin records 0x00811CC0..0x0081723F, D_00250F30 (0x2250),
  D_00275670 (0x30), the scratchpad 0x70003A40 (0x100), 0x70003B60 and
  0x70003B70 (4 each); read only: D_00241010 (8), D_0026E510, D_0026E850 and
  D_0026E880 (16 each);
- the static world's: D_00253560 (0x90, block 1 of static_world.emsw; its
  0x253570..AF and 0x2535B8 are rewritten every frame), D_00817240 (0x80,
  zero at start), the scratchpad 0x70003400 (0x80, zero at start), the
  state block D_008101D0..DF (0x10; the bind zeroes it, as 001AF690 does at
  0x1AE040 state 0, the step the binder binds at) and, read only, the bank
  (block 0 of static_world.emsw at its original address 0x01516F40,
  0x2D6FE0 bytes; `em_rcl_static_world_load`, called by the binder's
  rcl_bind: fail-stop when the export is missing);
- read only, the external views: D_00810E80 (2), D_00810610 (0x40),
  D_008105E0 (0x10), D_008106B0 (0x48), D_00810700..702, D_008101E4 (1),
  0x70003B8D (1), D_008102B0 (0x320), D_00810E88 (2), D_008106C4 (1) and
  D_0028A5A0 (4): the bank's address word, which is slot 0x44 of the
  screen-module loader's resource table (em_module_loader's `d28A490`, the
  one storage of D_0028A490..; the export seeds it with 0x01516F40, the
  value every AREA11 capture holds).

Rehearsal E of test_static_world_reference runs 001C1D00 over exactly this
set, marking included.

### 5.3 Host workers

All eight EmSwcHostWorkers are NULL: reaching one faults (001D5BD0: key
0x0B00 is not in its list; the sound branch 00122BB8, 001D73A0, 001D72D0,
001D75E0, 001CEFD0: flag 0x23 is clear, +0x174 = 3 in every capture;
001E1760 / 001E17E0: flag 0x22 is cleared by 001C1F50 for key 0x0B00).

### 5.4 The background gate

em_render_frame.c `background_gate` reads 001E0DF0's own condition: step
V's gate (`em_rcl_001D2300_calls_001E0DF0`, with the movie frame's flag 4)
and the context's +0x1D8 word; flag 0x21 no longer stands in for it. The
draw's TEX0 and RGBAQ are the ones the list's own A+D packets write
(001D6F60, 001D7080; `em_static_world_live_background_state` reads them
from the list's bytes up to its RET), and the loaded background asset must
hold exactly those (fail-stop otherwise). em_background_gs still draws the
grid kernel 0x0023C990 natively (BACKGROUND.md); its matrix is computed
from the frame head's view as 001E1E60 computes D_00253570, not read from
that storage (section 6).

## 6. Known gaps

- **001E1AD0's 001E1760 and 001E17E0 are not translated.** Nor are
  001E1760's 001D6E60 path, 001E10A0 and 001E13E0. They are host workers
  (flag 0x22; the first level never sets it), bound to a fault. The unit
  oracle stubs them on both sides.
- **The flag-0x23 sound branch's callees are not translated** (00122BB8,
  001D73A0, 001D72D0, 001D75E0, 001CEFD0). They are host workers, bound to
  a fault; their arguments, including the id and level blocks, are compared
  by the unit oracle.
- **001D5BD0 is not bound** (a fault when reached). The AREA01 lane's
  verified em_area01_render_001D5BD0 is not in the Makefile.
- **The scratchpad matrices 0x70003400..7F are not compared with the
  captures** (they are reused later in the frame). They are compared with
  the original, run over the same input, and the level smoke's samples
  re-execute 001C1D00 over them.
- **The 001D5370 clip rule (VCLIPW) is em_render_context's documented
  model** (RENDER_CONTEXT.md section 4). It is the same on both sides of the
  tests.
- **em_background_gs recomputes 001E1E60's matrix.** The background draw
  (the grid kernel 0x0023C990's native model) builds its matrix from the
  frame head's view and zoom, as 001E1E60 builds D_00253570 / D_002535B8;
  it does not read that storage. test_background_reference compares the
  model with the captures.
- **Frame cost (measured; fixed in the C8 fix round).** 001C1D00 and the
  run's draw run every world frame on the CPU. With the integer float
  model's lanes (em_ee_float.h) the level kernel alone cost up to 7.7 ms of
  CPU on the heaviest captured run (08_truck_crossing: 600 level and 67
  clip batches, 4,292 triangles) and the first level no longer held the
  16.68 ms NTSC tick: the capped newgame-control run took 25.6 s for its
  1,301 locked ticks (about 51 Hz; the review measured 25.48 s) and 639 of
  its 1,331 in-level ticks were over the period.
  The fix keeps every result and changes only how the lanes are computed:
  - `em_vu_host_lanes.h`: a VU multiply / add / subtract / reciprocal on
    finite operands is one host FPU instruction in round-toward-zero,
    flush-to-zero (arm64 FPCR RMode 3 + FZ; x86-64 MXCSR RC 3 + FTZ + DAZ),
    which is exactly the model's rule (exact result truncated, FTZ, +-MAX
    on overflow; a zero divisor handled as the model). The level kernel
    runs one batch text with `host` 0 (the model: tests, the shadow's box)
    or 1 (em_vu1_level_kernel_batch_host, the live walk); the object and
    face kernels take the host lanes in em_object_unit.c
    (EMVUO_HOST_LANES). A faulting level batch is re-run on the model, so
    fault codes and vertices are the model's too.
  - `em_ee_float.h` (every user): the bit length is one count-leading-zeros,
    the exact product knows its 47- or 48-bit width, and the VU form table
    is binary-searched; old against new header over 50 million random and
    edge operands and every (op, dest, bc) form: identical
    (`test-ee-float-header` quick and full pass).
  Proof: `test-vu-host-lanes` (55,283 boundary + 400,000 random lane
  operations; EM_TEST_FULL=1 8,000,000: every one equal to the model, and
  the environment shown to be in effect); `test-static-world-draw-reference`
  (every captured run walked with the host lanes equals the original
  microcode; every synthetic batch run both ways, equal word for word);
  `test-object-unit-reference` EM_TEST_FULL=1 (375 owner draws and 60 face
  units through em_object_unit_run with the host lanes, every triangle
  equal to the original microcode's); the level smoke.
  Measured on the user's M1 (`EM_FRAME_TIMING=<file>`, em_frame.c: per
  step wall and main-thread CPU time, the pacing sleep excluded; headless
  newgame-control, "before" is this step's build with the integer lanes,
  both at machine load average about 5..12):

  | newgame-control, in-level ticks (1,331) | before | after |
  |---|---|---|
  | main-thread CPU per tick, mean | 15.9 ms | 5.6 ms |
  | p95 / p99 / max | 29.0 / 29.3 / 29.7 ms | 9.0 / 9.2 / 9.5 ms |
  | ticks over 16.68 ms | 472 | 0 |
  | the 30 moving ticks, mean | 18.0 ms | 6.2 ms |
  | capped run, "holding W" to "fade clear" | 25.90 s | 22.79 s |

  The capped window is 1,366 paced steps, not 1,301 (it also holds the
  steps between the print and the first locked tick and the fade): 22.79 s
  is 1,366.0 NTSC periods, so every step in it met its deadline (the
  capped run's timing file: no step over the period after step 1,537; the
  four over it are the loads before the window). The review measured HEAD
  (the legacy meshes) at 22.80 s over the same window. Under the heavy
  load of other lanes (load average 140..190) the after-build still kept
  every in-level tick under the period (CPU mean 6.2 ms, max 10.7 ms).
  The heaviest captured run's walk (08_truck_crossing) now costs 1.8 ms of
  CPU (level kernel 38 %, the walk's DMA / GIF / GS queue about 30 %, the
  clip kernel about 17 %). Remaining CPU in the frame is mostly the object
  units' walk and 001D5370's VU0 corner tests (em_vu_vec_bits).
- **The trace hook exists only for the test.** It is NULL in the game.
- **Decomp-side registry (not this lane's files).** The decomp's
  docs/FUNCTIONS.csv lists 001D4A90 as `readable`, but its C
  (src/func_001D4A90.c) is wrong: it keeps one run length for every run,
  stores through a bogus pointer and dereferences *D_00275670 (the review
  confirmed it against the .s; this lane translated from the .s). It also
  lists 001E0DF0 as `undecompiled`, although src/func_001E0DF0.c is a
  byte-matched C body. Both rows need a decomp-side correction.

## 7. The static world drawn from its packets

### 7.1 What the run sends

The channel-0 run 001D5370 writes (section 2.2) is the first thing channel 0
holds in a world frame; step V's list sends channel 0 right after the
channel-3 background. The DMAC walks it with tag transfer off (the upper
eight bytes of a tag are never written; the captures hold stale bytes
there). Its shapes, read from every captured run:

- CNT (001D4750): a FLUSH and UNPACK V4-32 of 8 qwords to VU address 0 (the
  scratchpad view projection D_70003AC0 and the first four rows of
  D_00817240), then a CNT with UNPACK of 4 qwords to 0x3F5;
- REF 1 qword to *D_00275674 (a VIF FLUSH);
- CALL (qwc 0) to the kernel packet 0x00237180 or, for 001D4960, 0x00239C90;
- REF 9 qwords to D_00815360 (001D1F80(0, 1, 0): set 1, class 0 of the GS
  state bank: TEST 0x5000D, TEX1 0x60, ZBUF ZMSK 0, ALPHA 0x80000000A8,
  CLAMP 0, COLCLAMP 1);
- REF 8 qwords to the skin record D_00816440 + (context +0x9C << 7): STCYCL
  4,4 and UNPACK V4-32 of 7 qwords to 0x3F9..0x3FF (the clip kernel's tags,
  the template, the fog row and the guard rows);
- REFs of an object's blocks (runs of up to 0x1F8 blocks of 0x82 qwords):
  each block is STCYCL 4,4, UNPACK V4-32 of 128 qwords to TOPS, 32 vertices
  (TEX0, (s, t, 1, 0), the colour / 128, position + data word) and MSCAL 0
  (an object's first block) or MSCNT.

The kernel packets (read from the ELF by the draw test) set STCYCL 4,4,
BASE 0x190 and OFFSET 0x109 (level) / 0x101 (clip), which returns the VIF
double buffer to BASE; every MSCAL / MSCNT flips it. The run's first UNPACK
inherits the cycle the list left: the channel-3 list ends with the
background kernel packet 0x0023C990's STCYCL 4,4.

### 7.2 The level kernel (em_vu1_level_kernel.h)

The 79-instruction program at 0x002371B0: per vertex c = p x M (M = dmem
0..3), Q = 1 / c.w, the guard-band CLIP history, s = c.xyz Q, fog, the
back-face cull (the S flag of the screen cross product times the data
word's float value, the strip's winding), ADC = data bit 15 | CLIP of
vertices i-2..i | cull; out: TEX0 (the vertex's), ST = (s, t, 1) Q,
RGBAQ = the colour + 65536.0 (its low byte per lane), XYZF2 = ftoi4(s.xyz,
fog [+ 2048 when ADC]); the packet (template + 32 x 4 qwords) is kicked at
TOP + 0x84. An MSCNT re-runs the whole set-up (the program's last
instruction branches to micro 0). The loop is software-pipelined: the
carried registers (the previous batch's last screen point and edge, the w
iteration 30 loaded, the four output registers) are modelled; a vertex 0 or
1 whose ADC would depend on registers another program left faults
(STALE): every block of the AREA11 bank carries data bit 15 on both. The
drop shadow's box pass (001DA310) runs the same program: its
em_shadow_gs_level_batch is now an adapter over this translation (one
owner). The batch is one text instantiated twice: on the model's integer
lanes (em_vu1_level_kernel_batch) and on the host FPU lanes
(em_vu1_level_kernel_batch_host, em_vu_host_lanes.h), which the live walk
uses (section 6).

### 7.3 The walk and the draw (em_static_world_draw, em_static_world_live)

em_static_world_draw walks [start, end) over the render context's storage
and the bank (em_rcl_bytes): CNT, REF and the kernel CALLs; the VIF codes
NOP, STCYCL, FLUSH*, UNPACK V4-32 (to an address or, with FLG, TOPS),
MSCAL 0, MSCNT and DIRECT (only inside the GS state REF, which must be
exactly the class-0 packet, em_object_unit_gs_state_check); MSCAL / MSCNT
run the level kernel's translation or the clip kernel's
(em_vu1_shadow_clip_run, EM_VU1_CLIP_BOX: 00239C90 is the box's clip
kernel); every XGKICK's PACKED packet goes through the GS vertex queue
(strips under ADC, triangle lists) and every drawn triangle is handed to
`em_gfx_gs_opaque` in GS order with the state in force. Every other tag,
code, register or program faults (fail-stop).

frame_close_out calls `em_static_world_live_draw` after the background and
the frame's fog, before the owner walk's units: channel 0's order.
`em_gfx_gs_opaque` (em_gfx.h) draws the triangles with the object units'
class-0 pixel path (Metal: the same pipeline, position from the GS words
through em_background_gs_ndc and the GS-depth mapping, bilinear REPEAT with
4-bit weights, alpha test > 0, the measured fog blend, no blending), with
the TEX0's texture function: every static-world TEX0 is MODULATE (TFX 0,
TCC 1): Cv = min(Ct Cf >> 7, 255), Av = min(At Af >> 7, 255). The 119
bank TEX0s are in the object texture export (tools/export_object_textures.py
now reads the bank too; each decodes identically from the GS memory of the
15 route captures).

The shadow chain's box and receivers are positioned from their own kicked
words the same way (SHADOW_ORIGINAL.md), so a receiver meets the level
surface it lies on at the same depth, as on the GS.

### 7.4 Retired

- **The legacy level meshes.** AREA11 loads no scene EMDL any more
  (em_scene.c scene_load): 00_zone_main .. 04_zone_e4 held exactly the
  bank's geometry (every record the old exporter found lies in a bank
  block; five bank vertices it missed), and 05_movables held the bank's
  last 181 vertices plus three skinned models (resources 0x50, 0x06, 0x5A
  of f17) that the old exporter placed at the world origin and that no
  captured frame REFs. Their draw (the chain's scene entries, the baked
  vertex colour and the native projection) is gone from the first level.
  The legacy path stays for scenes without the render context.
- **em_render_001C1D00 on the first level** (it stays for scenes without the
  render context), the flag-0x21 stand-in in background_gate, UM_001D52E0 and
  `rcl_grid_header`.

## 8. Evidence

- **test_static_world_reference** (section 4): the builders against the
  original instructions; rehearsal E now over exactly the live 27 views.
- **test_static_world_draw_reference** (`make
  test-static-world-draw-reference`, about 2 s; EM_TEST_FULL=1 about 9 s):
  A: the kernel packets' VIF codes from the ELF. B: every captured run (the
  opening, 00..14, the c7cap capture) replayed on one persistent VU1 with
  the ORIGINAL microcode and walked natively: every XGKICK equal byte for
  byte (program, TOP, packet; the level packets' ST w lane after a clip
  batch, the clip kernel's register, masked), and every triangle equal (an
  independent GS walk). 17 of 17 runs: 0xB00..0x3150 bytes, 36..321 object
  REFs, 45..600 level and 2..67 clip batches, 237..4,292 triangles. C: 576
  synthetic batches (every ADC reason, both fog clamps, the carried
  registers of vertices 0 and 1, a zero cross product culled by its -0)
  equal over the whole data memory; STALE and OPERAND faults; each also run
  on the host lanes and equal to the model's word for word, faults
  included. B and D walk with the host lanes. D: fail-stop cases.
- **test_vu_host_lanes** (`make test-vu-host-lanes`): the host lanes
  against em_ee_float.h (section 6).
- **test_static_world_gpu** (`make test-static-world-gpu`): the Metal pixel
  path over 05_boxes' run (1,187 triangles) against the GS pixel model over
  the original microcode's triangles: 99.78 % of 1,007,297 interior pixels
  exact, 100 % within 2.
- **The level smoke** (check_static_world, LEVEL_SMOKE.md): over the whole
  route every 001C1D00 run is drawn in its tick (12,572 runs on the main
  line); 32 sampled calls re-executed: the ORIGINAL 001C1D00 over the
  port's inputs writes the port's channel-0 run, channel-3 list and
  D_00253560 byte for byte, and the original microcode draws the port's
  triangles; at the view-exact snapshots 10 and 14 the port's triangles are
  the ones the capture's own run draws (2,457 and 847); a main-line run
  that reaches Roger must compare both (VIEW_EXACT_MAIN_LINE), so a camera
  regression cannot drop them silently. check_render_context
  compares the seven-tag world list with the captures'.
- **Pixels.** By eye: the first-control frame before and after (the legacy
  meshes against the packets: the same geometry; the railing bars now draw
  solid, without the bright fringes, as the fb2 reference frame shows) and
  05_boxes' run against its fb2 frame. The shadow harness
  (`test_shadow_original_reference.py --capture`, now drawing the beat's
  own run, and since audit 1b item 5 the beat's own sky grid) gives level pixels around the shadow within 1 of the original
  screenshot's means (01, 08, 12).
